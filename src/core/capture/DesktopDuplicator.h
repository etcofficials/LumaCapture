#pragma once

#include "capture/CaptureSource.h"
#include "capture/PointerShape.h"
#include "gpu/CursorOverlay.h"
#include "gpu/D3D11Device.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <memory>
#include <vector>

namespace luma::capture {

using Microsoft::WRL::ComPtr;

// Wraps IDXGIOutput1::DuplicateOutput for one monitor (used for display and
// region capture; the region crop is applied by the compositor).
// New desktop images are copied into a persistent, shader-readable texture
// so the compositor can read them at any later capture tick; the cursor shape
// is decoded and uploaded to a CursorOverlay.
class DesktopDuplicator final : public CaptureSource {
public:
    DesktopDuplicator(gpu::D3D11Device& device, bool captureCursor);
    ~DesktopDuplicator() override;
    DesktopDuplicator(const DesktopDuplicator&) = delete;
    DesktopDuplicator& operator=(const DesktopDuplicator&) = delete;

    Result acquire(unsigned timeoutMs) override;
    bool reopen() override;
    bool isOpen() const override { return m_dup != nullptr; }

    unsigned width() const override { return m_width; }
    unsigned height() const override { return m_height; }
    bool hasImage() const override { return m_haveImage; }
    bool takeDirty() override;
    ID3D11ShaderResourceView* srv() const override { return m_desktopSrv.Get(); }

    const gpu::CursorOverlay* cursorOverlay() const override { return m_captureCursor ? m_cursor.get() : nullptr; }
    gpu::CursorPlacement cursorPlacement() const override { return m_placement; }
    POINT screenOrigin() const override { return m_origin; }
    std::string status() const override;
    uint64_t imageUpdates() const override { return m_copies; }

private:
    void open();
    void readDesc();
    void ensureDesktopTexture(unsigned w, unsigned h);
    void releaseHeldFrame();

    gpu::D3D11Device& m_device;
    bool m_captureCursor;
    ComPtr<IDXGIOutputDuplication> m_dup;
    bool m_holdingFrame = false;

    ComPtr<ID3D11Texture2D> m_desktop;
    ComPtr<ID3D11ShaderResourceView> m_desktopSrv;
    unsigned m_width = 0, m_height = 0;
    POINT m_origin{};
    bool m_haveImage = false;
    bool m_dirty = false;
    uint64_t m_copies = 0;

    std::unique_ptr<gpu::CursorOverlay> m_cursor;
    gpu::CursorPlacement m_placement;
    std::vector<uint8_t> m_shapeBuffer;
    CursorImage m_cursorImage;
};

} // namespace luma::capture
