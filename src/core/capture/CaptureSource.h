#pragma once

#include "gpu/CursorOverlay.h"


#include <d3d11.h>
#include <windows.h>

#include <cstdint>
#include <string>

namespace luma::capture {

// A GPU image source sampled by the capture loop on every tick. Implementations:
// DesktopDuplicator (monitor / region) and WindowCapture (Windows.Graphics.Capture).
// All methods are called on the capture thread only.
class CaptureSource {
public:
    enum class Result {
        NewFrame, // image and/or cursor changed
        Timeout,  // nothing changed within the timeout
        Lost,     // source invalidated; call reopen()
    };

    virtual ~CaptureSource() = default;

    // Waits up to timeoutMs for a change. Throws HResultError on real failures.
    virtual Result acquire(unsigned timeoutMs) = 0;
    // Tries to recover after Lost; false while that is temporarily impossible.
    virtual bool reopen() = 0;
    virtual bool isOpen() const = 0;

    virtual unsigned width() const = 0;
    virtual unsigned height() const = 0;
    virtual bool hasImage() const = 0;
    // True if the image or cursor changed since the previous call (consumes the flag).
    virtual bool takeDirty() = 0;
    virtual ID3D11ShaderResourceView* srv() const = 0;

    // Cursor bitmap the compositor should draw, or nullptr when the source
    // already contains the cursor (window capture) or cursor capture is off.
    virtual const gpu::CursorOverlay* cursorOverlay() const = 0;
    virtual gpu::CursorPlacement cursorPlacement() const = 0;

    // Screen coordinates of source pixel (0,0): maps GetCursorPos() into the source.
    virtual POINT screenOrigin() const = 0;
    // Human-readable state for the UI ("window minimized", ...). Empty when normal.
    virtual std::string status() const { return {}; }
    // Number of new images received so far.
    virtual uint64_t imageUpdates() const = 0;
};

} // namespace luma::capture
