// LumaCapture compositor: source (display/window/region) + filters + cursor
// effects + cursor + webcam + overlay layer, evaluated per output pixel and
// written directly as NV12 planes (no intermediate full-size RGB frame):
//   PS_Y         Y  plane, R8_UNORM   at output size
//   PS_UV        UV plane, R8G8_UNORM at half output size (R = Cb, G = Cr)
//   PS_Composite BGRA at output size, for the --cpu-convert path
//   PS_Preview   small BGRA live-preview image, sampled from the finished NV12 planes
// Colour: BT.709 limited range, matching the encoder tags.

cbuffer Params : register(b0)
{
    float2 outSize;  float2 srcSize;
    float4 srcCrop;        // x, y, w, h in source pixels
    float4 destRect;       // x, y, w, h in output pixels (letterboxed placement)
    float2 cursorPos;  float2 cursorSize;   // source pixels; size 0 = no cursor bitmap
    float2 mousePos;   float hlRadius;  float hlEnabled;
    float4 hlColor;
    float4 clicks[4];      // x, y (source px), progress 0..1, button (0 none, 1 left, 2 right)
    float4 clickColorL;
    float4 clickColorR;
    float clickRadius; float fxEnabled; float brightness; float contrast;
    float saturation;  float invGamma;  float temperature; float tint;
    float sharpen;     float blurRadius; float grayscale; float srcScale; // source px per output px
    float4 camRect;        // x, y, w, h in output pixels
    float4 camUv;          // u0, v0, u1, v1 (u0 > u1 = mirrored)
    float4 camBorderColor;
    float camEnabled;  float camShape;  float camRadius; float camBorder;
    float camOpacity;  float overlayEnabled; float2 pad0;
    float4 overlayBounds;  // x0, y0, x1, y1 in output pixels
};

Texture2D<float4> sourceTex   : register(t0);
Texture2D<float4> cursorColor : register(t1); // straight alpha
Texture2D<float>  cursorInv   : register(t2); // 1 = invert the pixel underneath
Texture2D<float4> camTex      : register(t3); // straight alpha (chroma key)
Texture2D<float4> overlayTex  : register(t4); // premultiplied alpha
SamplerState linearClamp      : register(s0);

struct VSOut { float4 pos : SV_Position; };

VSOut VSMain(uint id : SV_VertexID)
{
    float2 t = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float3 src(float2 uv) { return sourceTex.SampleLevel(linearClamp, uv, 0).rgb; }

// Base sample of the source at an output pixel. When downscaling, one bilinear tap
// only sees 2x2 source pixels and aliases fine text (shimmering, broken strokes);
// four taps spread over the output pixel's footprint approximate an area filter.
float3 srcScaled(float2 uv)
{
    [branch] if (srcScale > 1.25) {
        float2 d = (0.25 * srcScale) / srcSize;
        return 0.25 * (src(uv + float2(-d.x, -d.y)) + src(uv + float2(d.x, -d.y)) +
                       src(uv + float2(-d.x, d.y)) + src(uv + d));
    }
    return src(uv);
}

float3 sampleSource(float2 srcPx)
{
    float2 uv = srcPx / srcSize;
    float3 c = srcScaled(uv);
    [branch] if (blurRadius > 0) {
        float2 d = blurRadius / srcSize;
        c += src(uv + float2(d.x, 0)) + src(uv - float2(d.x, 0)) + src(uv + float2(0, d.y)) + src(uv - float2(0, d.y));
        c += src(uv + d) + src(uv - d) + src(uv + float2(d.x, -d.y)) + src(uv + float2(-d.x, d.y));
        c /= 9.0;
    } else if (sharpen > 0) {
        float2 d = max(srcScale, 1.0) / srcSize;
        float3 n = src(uv + float2(d.x, 0)) + src(uv - float2(d.x, 0)) + src(uv + float2(0, d.y)) + src(uv - float2(0, d.y));
        c = saturate(c + sharpen * (c - n * 0.25));
    }
    return c;
}

float3 applyFilters(float3 c)
{
    c *= float3(1.0 + 0.15 * temperature, 1.0 - 0.10 * tint, 1.0 - 0.15 * temperature);
    c = (c - 0.5) * contrast + 0.5 + brightness;
    float y = dot(saturate(c), float3(0.2126, 0.7152, 0.0722));
    c = lerp(y.xxx, c, grayscale > 0.5 ? 0.0 : saturation);
    return pow(saturate(c), invGamma);
}

float sdRoundRect(float2 p, float2 halfSize, float r)
{
    float2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float3 compose(float2 outPx)
{
    float3 c = float3(0, 0, 0); // letterbox
    float2 rel = (outPx - destRect.xy) / destRect.zw;
    [branch] if (all(rel >= 0) && all(rel <= 1)) {
        float2 srcPx = srcCrop.xy + rel * srcCrop.zw;
        c = sampleSource(srcPx);
        [branch] if (fxEnabled > 0.5)
            c = applyFilters(c);

        float aa = max(srcScale, 1.0);
        [branch] if (hlEnabled > 0.5) {
            float d = length(srcPx - mousePos);
            c = lerp(c, hlColor.rgb, hlColor.a * saturate((hlRadius - d) / aa));
        }
        [unroll] for (int i = 0; i < 4; ++i) {
            float4 k = clicks[i];
            [branch] if (k.w > 0.5) {
                float r = clickRadius * (0.35 + 0.65 * k.z);
                float d = abs(length(srcPx - k.xy) - r);
                float4 col = k.w < 1.5 ? clickColorL : clickColorR;
                c = lerp(c, col.rgb, col.a * (1.0 - k.z) * saturate((2.5 * aa - d) / aa));
            }
        }
        float2 cp = srcPx - cursorPos;
        [branch] if (all(cp >= 0) && all(cp < cursorSize)) {
            int3 ip = int3(int2(cp), 0);
            float4 cc = cursorColor.Load(ip);
            c = lerp(c, cc.rgb, cc.a);
            if (cursorInv.Load(ip) > 0.5)
                c = 1.0 - c;
        }
    }

    [branch] if (camEnabled > 0.5) {
        float2 halfSize = camRect.zw * 0.5;
        float2 p = outPx - (camRect.xy + halfSize);
        float r = camShape > 1.5 ? min(halfSize.x, halfSize.y) : (camShape > 0.5 ? camRadius : 0.0);
        float dOuter = sdRoundRect(p, halfSize, r);
        [branch] if (dOuter < 1.0) {
            float outerA = saturate(0.5 - dOuter);
            float2 innerHalf = max(halfSize - camBorder, 1.0);
            float2 local = saturate((p + innerHalf) / (2.0 * innerHalf));
            float4 cam = camTex.SampleLevel(linearClamp, lerp(camUv.xy, camUv.zw, local), 0);
            float3 inner = lerp(c, cam.rgb, cam.a);
            float3 camCol = inner;
            [branch] if (camBorder > 0) {
                float innerA = saturate(0.5 - (dOuter + camBorder));
                camCol = lerp(lerp(c, camBorderColor.rgb, camBorderColor.a), inner, innerA);
            }
            c = lerp(c, camCol, outerA * camOpacity);
        }
    }

    [branch] if (overlayEnabled > 0.5 && outPx.x >= overlayBounds.x && outPx.y >= overlayBounds.y &&
                 outPx.x <= overlayBounds.z && outPx.y <= overlayBounds.w) {
        float4 o = overlayTex.SampleLevel(linearClamp, outPx / outSize, 0);
        c = o.rgb + c * (1.0 - o.a);
    }
    return c;
}

static const float3 kY  = float3( 0.2126,  0.7152,  0.0722);
static const float3 kCb = float3(-0.1146, -0.3854,  0.5000);
static const float3 kCr = float3( 0.5000, -0.4542, -0.0458);

float PS_Y(VSOut i) : SV_Target
{
    return dot(compose(i.pos.xy), kY) * (219.0 / 255.0) + (16.0 / 255.0);
}

float2 PS_UV(VSOut i) : SV_Target
{
    // One sample at the centre of the 2x2 luma block (bilinear = block average at 1:1).
    float3 c = compose(i.pos.xy * 2.0);
    return float2(dot(c, kCb), dot(c, kCr)) * (224.0 / 255.0) + (128.0 / 255.0);
}

float4 PS_Composite(VSOut i) : SV_Target
{
    return float4(compose(i.pos.xy), 1.0);
}

// ---------------------------------------------------------------- live preview
// Reads the NV12 planes that were just rendered (exactly what is encoded) and
// converts them back to RGB at preview size.
cbuffer PreviewParams : register(b1)
{
    float2 previewSize; float2 previewPad;
};
Texture2D<float>  yPlane  : register(t5);
Texture2D<float2> uvPlane : register(t6);

float4 PS_Preview(VSOut i) : SV_Target
{
    float2 uv = i.pos.xy / previewSize;
    // Downscale with four taps over the preview pixel's footprint (one preview pixel
    // spans 1/previewSize in texture coordinates; see srcScaled).
    float2 d = 0.25 / previewSize;
    float y = 0.25 * (yPlane.SampleLevel(linearClamp, uv + float2(-d.x, -d.y), 0) +
                      yPlane.SampleLevel(linearClamp, uv + float2(d.x, -d.y), 0) +
                      yPlane.SampleLevel(linearClamp, uv + float2(-d.x, d.y), 0) +
                      yPlane.SampleLevel(linearClamp, uv + d, 0));
    float2 c = uvPlane.SampleLevel(linearClamp, uv, 0) - (128.0 / 255.0);
    y = (y - 16.0 / 255.0) * (255.0 / 219.0);
    c *= 255.0 / 224.0;
    // BT.709 limited-range Y'CbCr -> R'G'B' (inverse of kY/kCb/kCr above).
    float3 rgb = float3(y + 1.5748 * c.y, y - 0.1873 * c.x - 0.4681 * c.y, y + 1.8556 * c.x);
    return float4(saturate(rgb), 1.0);
}
