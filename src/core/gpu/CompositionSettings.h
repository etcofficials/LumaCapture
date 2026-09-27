#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace luma::gpu {

struct Rgba {
    float r = 1, g = 1, b = 1, a = 1;
};

// Colour adjustments applied to the captured screen image (not the webcam,
// which has its own CPU filter chain). All neutral by default.
struct VideoFilterSettings {
    bool enabled = false;
    float brightness = 0;  // -0.5 .. 0.5
    float contrast = 1;    // 0.5 .. 1.5
    float saturation = 1;  // 0 .. 2
    float gamma = 1;       // 0.5 .. 2
    float temperature = 0; // -1 (cool) .. 1 (warm)
    float tint = 0;        // -1 (green) .. 1 (magenta)
    float sharpen = 0;     // 0 .. 1
    float blur = 0;        // blur radius in source pixels, 0 = off
    bool grayscale = false;
};

struct CursorEffects {
    bool highlight = false;
    Rgba highlightColor{1.0f, 0.85f, 0.1f, 0.35f};
    float highlightRadius = 28; // source pixels
    bool clicks = false;
    Rgba leftClickColor{0.25f, 0.6f, 1.0f, 0.9f};
    Rgba rightClickColor{1.0f, 0.35f, 0.3f, 0.9f};
    float clickRadius = 30;     // source pixels
};

enum class WebcamShape { Rectangle = 0, Rounded = 1, Circle = 2 };

// Webcam placement in the output frame; coordinates are fractions of the
// output width/height so they survive resolution changes.
struct WebcamPlacement {
    bool enabled = false;
    float x = 0.74f, y = 0.70f, w = 0.24f, h = 0.24f;
    float cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0; // fractions of the camera image
    bool mirror = false;
    WebcamShape shape = WebcamShape::Rounded;
    float cornerRadius = 0.12f; // fraction of the shorter side (Rounded)
    float borderPx = 0;         // output pixels
    Rgba borderColor{1, 1, 1, 1};
    float opacity = 1;
};

// Pre-rendered text/image/watermark layer at output resolution (premultiplied BGRA).
struct OverlayImage {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint32_t> pixels;
    // Bounding box of non-transparent content; pixels outside are not sampled.
    int boundsX = 0, boundsY = 0, boundsW = 0, boundsH = 0;
};

struct CompositionSettings {
    VideoFilterSettings filters;
    CursorEffects cursor;
    WebcamPlacement webcam;
    std::shared_ptr<const OverlayImage> overlay; // null = no overlay layer
};

} // namespace luma::gpu
