#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

namespace luma::capture {

struct ScreenshotImage {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint32_t> pixels; // BGRA (alpha 255), top-down
};

// Screenshots use GDI (BitBlt from the composited desktop / PrintWindow),
// which works whether or not a recording is running and needs no GPU objects.
// Windows excluded with WDA_EXCLUDEFROMCAPTURE (LumaCapture's own windows) are
// not captured. Throws std::runtime_error on failure.
ScreenshotImage screenshotRect(const RECT& screenRect, bool includeCursor);
ScreenshotImage screenshotWindow(HWND window, bool includeCursor);

} // namespace luma::capture
