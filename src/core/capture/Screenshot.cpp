#include "capture/Screenshot.h"

#include <dwmapi.h>

#include <stdexcept>

namespace luma::capture {
namespace {

// RAII helpers for GDI objects.
struct ScreenDc {
    HDC dc = GetDC(nullptr);
    ~ScreenDc() { ReleaseDC(nullptr, dc); }
};
struct MemDc {
    HDC dc;
    explicit MemDc(HDC ref) : dc(CreateCompatibleDC(ref)) {}
    ~MemDc() { DeleteDC(dc); }
};
struct Dib {
    HBITMAP bmp = nullptr;
    void* bits = nullptr;
    Dib(HDC dc, int w, int h)
    {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    }
    ~Dib()
    {
        if (bmp)
            DeleteObject(bmp);
    }
};

void drawCursor(HDC dc, POINT origin)
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING))
        return;
    ICONINFO ii{};
    if (!GetIconInfo(ci.hCursor, &ii))
        return;
    DrawIconEx(dc, ci.ptScreenPos.x - static_cast<int>(ii.xHotspot) - origin.x,
               ci.ptScreenPos.y - static_cast<int>(ii.yHotspot) - origin.y, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
    if (ii.hbmMask)
        DeleteObject(ii.hbmMask);
    if (ii.hbmColor)
        DeleteObject(ii.hbmColor);
}

ScreenshotImage finish(const Dib& dib, int w, int h)
{
    ScreenshotImage img;
    img.width = static_cast<unsigned>(w);
    img.height = static_cast<unsigned>(h);
    const auto* px = static_cast<const uint32_t*>(dib.bits);
    img.pixels.assign(px, px + static_cast<size_t>(w) * h);
    for (auto& p : img.pixels)
        p |= 0xFF000000u;
    return img;
}

} // namespace

ScreenshotImage screenshotRect(const RECT& r, bool includeCursor)
{
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0)
        throw std::runtime_error("Empty screenshot area");
    ScreenDc screen;
    MemDc mem(screen.dc);
    Dib dib(screen.dc, w, h);
    if (!dib.bmp)
        throw std::runtime_error("CreateDIBSection failed");
    HGDIOBJ old = SelectObject(mem.dc, dib.bmp);
    const BOOL ok = BitBlt(mem.dc, 0, 0, w, h, screen.dc, r.left, r.top, SRCCOPY | CAPTUREBLT);
    if (ok && includeCursor)
        drawCursor(mem.dc, POINT{r.left, r.top});
    GdiFlush();
    SelectObject(mem.dc, old);
    if (!ok)
        throw std::runtime_error("BitBlt failed (is the desktop locked?)");
    return finish(dib, w, h);
}

ScreenshotImage screenshotWindow(HWND window, bool includeCursor)
{
    if (!IsWindow(window))
        throw std::runtime_error("The selected window no longer exists");
    if (IsIconic(window))
        throw std::runtime_error("The selected window is minimized");
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(window, &r);
    // Copy the window's area of the composited desktop; this shows exactly what
    // the user sees (PrintWindow misses content of many GPU-rendered apps).
    return screenshotRect(r, includeCursor);
}

} // namespace luma::capture
