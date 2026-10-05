#pragma once

#include "gpu/CompositionSettings.h"
#include "gpu/CursorOverlay.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace luma::gpu {

using Microsoft::WRL::ComPtr;

struct ClickRing {
    float x = 0, y = 0;   // source pixels
    float progress = 0;   // 0..1 animation
    int button = 0;       // 0 none, 1 left, 2 right
};

// Everything that changes per tick.
struct FrameInputs {
    ID3D11ShaderResourceView* source = nullptr;
    unsigned srcWidth = 0, srcHeight = 0;
    RECT crop{};                         // source pixels; empty = whole source
    const CursorOverlay* cursor = nullptr;
    CursorPlacement cursorPlacement;
    bool mouseValid = false;
    float mouseX = 0, mouseY = 0;        // source pixels
    std::array<ClickRing, 4> clicks{};
};

// GPU compositor (successor of the NV12 converter). Renders the composed
// frame directly into NV12 render targets (Y: R8, UV: R8G8 at half size), or
// into a BGRA target for the CPU conversion path. The source is scaled to fit
// the output with preserved aspect ratio (letterboxed).
class Compositor {
public:
    Compositor(ID3D11Device* device, unsigned outWidth, unsigned outHeight, bool bgraOutput);

    // Uploads the overlay layer when its pointer changed; stores the rest.
    void setSettings(ID3D11DeviceContext* ctx, const CompositionSettings& settings);
    // Uploads a new webcam frame (straight-alpha BGRA).
    void setWebcamFrame(ID3D11DeviceContext* ctx, const uint32_t* pixels, unsigned width, unsigned height);
    void clearWebcam() { m_camValid = false; }

    void render(ID3D11DeviceContext* ctx, const FrameInputs& in);

    // Live preview (NV12 output only): a small BGRA image converted back from the Y/UV
    // planes rendered by the last render() call - i.e. exactly what is being encoded.
    // ensurePreview() (re)creates the preview target; returns false on the BGRA path.
    bool ensurePreview(unsigned width, unsigned height);
    void renderPreview(ID3D11DeviceContext* ctx);
    ID3D11Texture2D* previewTexture() const { return m_previewTex.Get(); }
    unsigned previewWidth() const { return m_previewW; }
    unsigned previewHeight() const { return m_previewH; }

    ID3D11Texture2D* yTexture() const { return m_yTex.Get(); }
    ID3D11Texture2D* uvTexture() const { return m_uvTex.Get(); }
    ID3D11Texture2D* bgraTexture() const { return m_bgraTex.Get(); }
    unsigned outWidth() const { return m_outW; }
    unsigned outHeight() const { return m_outH; }

private:
    void draw(ID3D11DeviceContext* ctx, ID3D11PixelShader* ps, ID3D11RenderTargetView* rtv, unsigned w, unsigned h);

    ID3D11Device* m_device;
    unsigned m_outW, m_outH;
    bool m_bgra;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_psY, m_psUV, m_psComposite;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11Buffer> m_params;
    ComPtr<ID3D11Texture2D> m_yTex, m_uvTex, m_bgraTex;
    ComPtr<ID3D11RenderTargetView> m_yRtv, m_uvRtv, m_bgraRtv;
    ComPtr<ID3D11ShaderResourceView> m_ySrv, m_uvSrv; // NV12 planes as preview input

    ComPtr<ID3D11PixelShader> m_psPreview;
    ComPtr<ID3D11Buffer> m_previewParams;
    ComPtr<ID3D11Texture2D> m_previewTex;
    ComPtr<ID3D11RenderTargetView> m_previewRtv;
    unsigned m_previewW = 0, m_previewH = 0;

    CompositionSettings m_settings;
    std::shared_ptr<const OverlayImage> m_uploadedOverlay;
    ComPtr<ID3D11Texture2D> m_overlayTex;
    ComPtr<ID3D11ShaderResourceView> m_overlaySrv;
    bool m_overlayValid = false;

    ComPtr<ID3D11Texture2D> m_camTex;
    ComPtr<ID3D11ShaderResourceView> m_camSrv;
    unsigned m_camW = 0, m_camH = 0;
    bool m_camValid = false;
};

} // namespace luma::gpu
