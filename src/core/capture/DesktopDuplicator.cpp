#include "capture/DesktopDuplicator.h"

#include "util/HResult.h"
#include "util/Log.h"

namespace luma::capture {

DesktopDuplicator::DesktopDuplicator(gpu::D3D11Device& device, bool captureCursor)
    : m_device(device), m_captureCursor(captureCursor), m_cursor(std::make_unique<gpu::CursorOverlay>(device.device()))
{
    open();
}

DesktopDuplicator::~DesktopDuplicator()
{
    releaseHeldFrame();
}

void DesktopDuplicator::open()
{
    check(m_device.output()->DuplicateOutput(m_device.device(), &m_dup), "IDXGIOutput1::DuplicateOutput");
    readDesc();
}

void DesktopDuplicator::readDesc()
{
    DXGI_OUTDUPL_DESC desc{};
    m_dup->GetDesc(&desc);
    if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED)
        log::warn("Display is rotated ({}); captured frames are not un-rotated", static_cast<int>(desc.Rotation));
    DXGI_OUTPUT_DESC od{};
    m_device.output()->GetDesc(&od);
    m_origin = POINT{od.DesktopCoordinates.left, od.DesktopCoordinates.top};
    ensureDesktopTexture(desc.ModeDesc.Width, desc.ModeDesc.Height);
}

bool DesktopDuplicator::reopen()
{
    releaseHeldFrame();
    m_dup.Reset();
    ComPtr<IDXGIOutputDuplication> dup;
    const HRESULT hr = m_device.output()->DuplicateOutput(m_device.device(), &dup);
    if (hr == E_ACCESSDENIED || hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE || hr == DXGI_ERROR_SESSION_DISCONNECTED ||
        hr == DXGI_ERROR_UNSUPPORTED)
        return false; // temporary: secure desktop, too many duplicators, etc.
    check(hr, "IDXGIOutput1::DuplicateOutput (reopen)");
    m_dup = dup;
    readDesc();
    m_dirty = true;
    return true;
}

std::string DesktopDuplicator::status() const
{
    if (!m_dup)
        return "Display capture paused by Windows (secure desktop, mode change or full-screen switch); retrying";
    return {};
}

void DesktopDuplicator::ensureDesktopTexture(unsigned w, unsigned h)
{
    if (m_desktop && w == m_width && h == m_height)
        return;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    m_desktopSrv.Reset();
    m_desktop.Reset();
    check(m_device.device()->CreateTexture2D(&desc, nullptr, &m_desktop), "CreateTexture2D(desktop copy)");
    check(m_device.device()->CreateShaderResourceView(m_desktop.Get(), nullptr, &m_desktopSrv), "CreateSRV(desktop copy)");
    m_width = w;
    m_height = h;
    m_haveImage = false;
}

void DesktopDuplicator::releaseHeldFrame()
{
    if (m_holdingFrame && m_dup) {
        const HRESULT hr = m_dup->ReleaseFrame();
        if (FAILED(hr) && hr != DXGI_ERROR_ACCESS_LOST && hr != DXGI_ERROR_INVALID_CALL)
            log::warn("ReleaseFrame: {}", hresultToString(hr));
    }
    m_holdingFrame = false;
}

CaptureSource::Result DesktopDuplicator::acquire(unsigned timeoutMs)
{
    if (!m_dup)
        return Result::Lost;

    // Microsoft recommends holding the previous frame until just before the next acquire.
    releaseHeldFrame();

    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    HRESULT hr = m_dup->AcquireNextFrame(timeoutMs, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        return Result::Timeout;
    if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
        m_dup.Reset();
        return Result::Lost;
    }
    check(hr, "AcquireNextFrame");
    m_holdingFrame = true;

    bool changed = false;
    if (info.LastPresentTime.QuadPart != 0) {
        ComPtr<ID3D11Texture2D> tex;
        check(resource.As(&tex), "QueryInterface(ID3D11Texture2D)");
        m_device.context()->CopyResource(m_desktop.Get(), tex.Get());
        ++m_copies;
        m_haveImage = true;
        changed = true;
    }

    if (info.LastMouseUpdateTime.QuadPart != 0) {
        const gpu::CursorPlacement p{info.PointerPosition.Visible != FALSE, info.PointerPosition.Position.x,
                                     info.PointerPosition.Position.y};
        if (p.visible != m_placement.visible || p.x != m_placement.x || p.y != m_placement.y) {
            m_placement = p;
            changed = changed || m_captureCursor;
        }
    }

    if (info.PointerShapeBufferSize > 0) {
        if (m_shapeBuffer.size() < info.PointerShapeBufferSize)
            m_shapeBuffer.resize(info.PointerShapeBufferSize);
        DXGI_OUTDUPL_POINTER_SHAPE_INFO shape{};
        UINT required = 0;
        hr = m_dup->GetFramePointerShape(static_cast<UINT>(m_shapeBuffer.size()), m_shapeBuffer.data(), &required, &shape);
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            m_holdingFrame = false;
            m_dup.Reset();
            return Result::Lost;
        }
        check(hr, "GetFramePointerShape");
        decodePointerShape(shape, m_shapeBuffer.data(), m_cursorImage);
        m_cursor->upload(m_device.context(), m_cursorImage);
        changed = changed || m_captureCursor;
    }

    if (changed)
        m_dirty = true;
    return changed ? Result::NewFrame : Result::Timeout;
}

bool DesktopDuplicator::takeDirty()
{
    const bool v = m_dirty;
    m_dirty = false;
    return v;
}

} // namespace luma::capture
