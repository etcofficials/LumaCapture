#pragma once

#include "capture/CaptureSource.h"
#include "gpu/D3D11Device.h"

#include <memory>

namespace luma::capture {

// Captures one top-level window with Windows.Graphics.Capture (Windows 10 1903+).
// Works for occluded windows; a minimized window delivers no new frames, so the
// last image is repeated. Windows 10 always draws a yellow capture border
// around the window (it cannot be disabled before Windows 11).
class WindowCapture final : public CaptureSource {
public:
    WindowCapture(gpu::D3D11Device& device, HWND window, bool captureCursor);
    ~WindowCapture() override;
    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    static bool isSupported();

    Result acquire(unsigned timeoutMs) override;
    bool reopen() override;
    bool isOpen() const override;

    unsigned width() const override;
    unsigned height() const override;
    bool hasImage() const override;
    bool takeDirty() override;
    ID3D11ShaderResourceView* srv() const override;

    const gpu::CursorOverlay* cursorOverlay() const override { return nullptr; } // WGC draws the cursor itself
    gpu::CursorPlacement cursorPlacement() const override { return {}; }
    POINT screenOrigin() const override;
    std::string status() const override;
    uint64_t imageUpdates() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace luma::capture
