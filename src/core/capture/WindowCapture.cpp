#include "capture/WindowCapture.h"

#include "util/HResult.h"
#include "util/Log.h"
#include "util/StringUtil.h"

#include <dwmapi.h>
#include <inspectable.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <wrl/client.h>

#include <algorithm>
#include <atomic>

namespace luma::capture {

using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;

namespace {

[[noreturn]] void rethrow(const winrt::hresult_error& e, const char* what)
{
    throw HResultError(e.code(), std::string(what) + " (" + toUtf8(e.message()) + ")");
}

} // namespace

struct WindowCapture::Impl {
    gpu::D3D11Device& dev;
    HWND hwnd;
    bool cursor;

    wgd::Direct3D11::IDirect3DDevice rtDevice{nullptr};
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    winrt::event_token arrivedToken{};
    winrt::event_token closedToken{};
    winrt::Windows::Graphics::SizeInt32 poolSize{};
    HANDLE frameEvent = nullptr;
    std::atomic<bool> closed{false};

    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    unsigned w = 0, h = 0;
    bool haveImage = false;
    bool dirty = false;
    uint64_t updates = 0;

    Impl(gpu::D3D11Device& d, HWND window, bool captureCursor) : dev(d), hwnd(window), cursor(captureCursor)
    {
        frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!frameEvent)
            throw HResultError(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent");
    }

    ~Impl()
    {
        stop();
        if (frameEvent)
            CloseHandle(frameEvent);
    }

    void start()
    {
        try {
            ComPtr<IDXGIDevice> dxgi;
            check(dev.device()->QueryInterface(IID_PPV_ARGS(&dxgi)), "QueryInterface(IDXGIDevice)");
            winrt::com_ptr<::IInspectable> inspectable;
            check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()),
                  "CreateDirect3D11DeviceFromDXGIDevice");
            rtDevice = inspectable.as<wgd::Direct3D11::IDirect3DDevice>();

            auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            check(interop->CreateForWindow(hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item)),
                  "IGraphicsCaptureItemInterop::CreateForWindow");

            poolSize = item.Size();
            pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
                rtDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize);
            HANDLE ev = frameEvent;
            arrivedToken = pool.FrameArrived([ev](auto const&, auto const&) { SetEvent(ev); });
            closedToken = item.Closed([this](auto const&, auto const&) {
                closed = true;
                SetEvent(frameEvent);
            });
            session = pool.CreateCaptureSession(item);
            try {
                session.IsCursorCaptureEnabled(cursor); // Windows 10 2004+
            } catch (const winrt::hresult_error&) {
                log::warn("Window capture: cursor capture toggle not supported on this Windows build");
            }
            session.StartCapture();
            closed = false;
        } catch (const winrt::hresult_error& e) {
            rethrow(e, "Starting window capture");
        }
    }

    void stop()
    {
        try {
            if (pool && arrivedToken)
                pool.FrameArrived(arrivedToken);
            if (item && closedToken)
                item.Closed(closedToken);
            if (session)
                session.Close();
            if (pool)
                pool.Close();
        } catch (const winrt::hresult_error& e) {
            log::warn("Stopping window capture: {}", toUtf8(e.message()));
        }
        arrivedToken = {};
        closedToken = {};
        session = nullptr;
        pool = nullptr;
        item = nullptr;
        rtDevice = nullptr;
    }

    void ensureTexture(unsigned cw, unsigned ch)
    {
        if (tex && cw == w && ch == h)
            return;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = cw;
        desc.Height = ch;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        srv.Reset();
        tex.Reset();
        check(dev.device()->CreateTexture2D(&desc, nullptr, &tex), "CreateTexture2D(window copy)");
        check(dev.device()->CreateShaderResourceView(tex.Get(), nullptr, &srv), "CreateSRV(window copy)");
        w = cw;
        h = ch;
    }
};

WindowCapture::WindowCapture(gpu::D3D11Device& device, HWND window, bool captureCursor)
    : m(std::make_unique<Impl>(device, window, captureCursor))
{
    if (!IsWindow(window))
        throw std::runtime_error("The selected window no longer exists");
    m->start();
    // The first frame defines the source size; wait briefly for it.
    for (int i = 0; i < 20 && !m->haveImage; ++i)
        acquire(50);
    if (!m->haveImage) {
        // Minimized windows produce nothing; start with a small black image.
        RECT r{};
        GetClientRect(window, &r);
        m->ensureTexture(std::max<LONG>(2, r.right - r.left), std::max<LONG>(2, r.bottom - r.top));
    }
}

WindowCapture::~WindowCapture() = default;

bool WindowCapture::isSupported()
{
    try {
        return wgc::GraphicsCaptureSession::IsSupported();
    } catch (const winrt::hresult_error&) {
        return false;
    }
}

CaptureSource::Result WindowCapture::acquire(unsigned timeoutMs)
{
    if (m->closed || !IsWindow(m->hwnd)) {
        m->closed = true;
        Sleep(std::min(timeoutMs, 50u)); // keep repeating the last image
        return Result::Timeout;
    }
    WaitForSingleObject(m->frameEvent, timeoutMs);

    try {
        wgc::Direct3D11CaptureFrame frame{nullptr};
        while (auto next = m->pool.TryGetNextFrame()) {
            if (frame)
                frame.Close();
            frame = next;
        }
        if (!frame)
            return Result::Timeout;

        const auto size = frame.ContentSize();
        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> surface;
        check(access->GetInterface(IID_PPV_ARGS(&surface)), "IDirect3DDxgiInterfaceAccess::GetInterface");
        D3D11_TEXTURE2D_DESC sd{};
        surface->GetDesc(&sd);
        const unsigned cw = std::max(2u, std::min<unsigned>(static_cast<unsigned>(size.Width), sd.Width));
        const unsigned ch = std::max(2u, std::min<unsigned>(static_cast<unsigned>(size.Height), sd.Height));
        m->ensureTexture(cw, ch);
        const D3D11_BOX box{0, 0, 0, cw, ch, 1};
        m->dev.context()->CopySubresourceRegion(m->tex.Get(), 0, 0, 0, 0, surface.Get(), 0, &box);
        frame.Close();

        if (size.Width != m->poolSize.Width || size.Height != m->poolSize.Height) {
            m->poolSize = size; // window resized: future frames use the new size
            m->pool.Recreate(m->rtDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        }
        m->haveImage = true;
        m->dirty = true;
        ++m->updates;
        return Result::NewFrame;
    } catch (const winrt::hresult_error& e) {
        rethrow(e, "Window capture frame");
    }
}

bool WindowCapture::reopen()
{
    if (!IsWindow(m->hwnd))
        return false;
    m->stop();
    m->start();
    return true;
}

bool WindowCapture::isOpen() const { return m->session != nullptr; }
unsigned WindowCapture::width() const { return m->w; }
unsigned WindowCapture::height() const { return m->h; }
bool WindowCapture::hasImage() const { return m->haveImage; }
ID3D11ShaderResourceView* WindowCapture::srv() const { return m->srv.Get(); }
uint64_t WindowCapture::imageUpdates() const { return m->updates; }

bool WindowCapture::takeDirty()
{
    const bool v = m->dirty;
    m->dirty = false;
    return v;
}

POINT WindowCapture::screenOrigin() const
{
    RECT r{};
    if (SUCCEEDED(DwmGetWindowAttribute(m->hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        return POINT{r.left, r.top};
    GetWindowRect(m->hwnd, &r);
    return POINT{r.left, r.top};
}

std::string WindowCapture::status() const
{
    if (m->closed)
        return "The captured window was closed - repeating its last image. Choose another window after stopping.";
    if (IsIconic(m->hwnd))
        return "The captured window is minimized - repeating its last image until it is restored.";
    return {};
}

} // namespace luma::capture
