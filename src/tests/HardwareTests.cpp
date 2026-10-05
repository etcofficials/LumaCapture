// Hardware integration tests (run with --hw). Short and light: no stress, no long recordings.

#include "TestMain.h"

#include "capture/Screenshot.h"
#include "mux/Muxer.h"
#include "session/RecordingSession.h"
#include "webcam/WebcamCapture.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <format>
#include <future>
#include <thread>

using namespace luma;
using namespace std::chrono_literals;

// Child mode for crash_recovery: record, then die without finalising.
int childCrashRecord(const std::wstring& file)
{
    session::SessionConfig cfg;
    cfg.source.kind = session::SourceKind::Display;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.fps = 30;
    cfg.encoder.preset = "ultrafast";
    cfg.recordAudio = false;
    cfg.outputFile = file;
    auto s = std::make_unique<session::RecordingSession>(cfg);
    s->start();
    std::this_thread::sleep_for(3s);
    TerminateProcess(GetCurrentProcess(), 0xDEAD); // no stop(), no trailer: like a crash or power loss
    return 1;
}

namespace {

// Runs fn on a helper thread; returns false if it did not finish within `timeout`
// (the helper is then abandoned - the process exits at the end of the run).
template <typename F>
bool finishesWithin(std::chrono::milliseconds timeout, F fn, double* elapsedMs = nullptr)
{
    auto task = std::make_shared<std::packaged_task<void()>>(fn);
    auto fut = task->get_future();
    const auto t0 = std::chrono::steady_clock::now();
    std::thread([task] { (*task)(); }).detach();
    const bool done = fut.wait_for(timeout) == std::future_status::ready;
    if (elapsedMs)
        *elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (done)
        fut.get(); // propagate exceptions
    return done;
}

std::wstring firstCamera(std::string* name = nullptr)
{
    const auto cams = webcam::enumerateWebcams();
    if (cams.empty())
        throw std::runtime_error("no webcam connected");
    if (name) {
        name->clear();
        for (wchar_t c : cams.front().name)
            name->push_back(c < 128 ? static_cast<char>(c) : '?');
    }
    return cams.front().symbolicLink;
}

} // namespace

TEST("hw", webcam_controls_and_pacing)
{
    const std::wstring link = firstCamera();
    auto cam = std::make_shared<webcam::WebcamCapture>();
    webcam::WebcamMode mode{1280, 720, 30, 1, "NV12"};
    cam->start(link, mode);
    const auto t0 = std::chrono::steady_clock::now();
    while (cam->state() != webcam::WebcamCapture::State::Running && std::chrono::steady_clock::now() - t0 < 6s)
        std::this_thread::sleep_for(20ms);
    CHECK_MSG(cam->state() == webcam::WebcamCapture::State::Running, cam->message());
    std::this_thread::sleep_for(1500ms); // let auto exposure settle
    for (const auto& c : cam->controls())
        luma::test::note(std::format("control {:<30} value {:>6}  range {}..{} step {} default {}{}", c.name, c.value,
                                     c.min, c.max, c.step, c.defaultValue,
                                     c.autoSupported ? (c.autoEnabled ? "  [auto ON]" : "  [auto off]") : ""));
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(1000ms);
        const auto s = cam->stats();
        luma::test::note(std::format("second {}: {:.0f} fps delivered, longest gap {:.1f} ms, filter chain {:.2f} ms/frame",
                                     i + 1, s.fps, s.maxGapMs, s.processMs));
    }
    cam->requestAnalysis();
    webcam::FrameStats st;
    for (int i = 0; i < 50 && !cam->takeAnalysis(st); ++i)
        std::this_thread::sleep_for(20ms);
    luma::test::note(std::format("image: mean luma {:.2f}, p2 {:.2f}, median {:.2f}, p98 {:.2f}, clipped hi {:.1f}% lo {:.1f}%, "
                                 "mid-tone RGB {:.2f}/{:.2f}/{:.2f}",
                                 st.meanLuma, st.p02, st.p50, st.p98, st.clippedHigh * 100, st.clippedLow * 100, st.meanR,
                                 st.meanG, st.meanB));
    cam->stop();
}

// Compares frame pacing / exposure with low-light exposure priority on and off,
// then restores the camera's original value.
TEST("hw", webcam_exposure_priority_effect)
{
    const std::wstring link = firstCamera();
    auto cam = std::make_shared<webcam::WebcamCapture>();
    cam->start(link, webcam::WebcamMode{1280, 720, 30, 1, "NV12"});
    const auto t0 = std::chrono::steady_clock::now();
    while (cam->state() != webcam::WebcamCapture::State::Running && std::chrono::steady_clock::now() - t0 < 6s)
        std::this_thread::sleep_for(20ms);
    CHECK_MSG(cam->state() == webcam::WebcamCapture::State::Running, cam->message());
    std::this_thread::sleep_for(1500ms);

    auto find = [&](webcam::CameraControl::Kind kind, long prop) {
        for (const auto& c : cam->controls())
            if (c.kind == kind && (kind != webcam::CameraControl::Kind::Camera || c.property == prop))
                return std::optional<webcam::CameraControl>(c);
        return std::optional<webcam::CameraControl>();
    };
    auto prio = find(webcam::CameraControl::Kind::ExposurePriority, 0);
    if (!prio) {
        luma::test::note("camera does not expose exposure priority - nothing to compare");
        cam->stop();
        return;
    }
    const long original = prio->value;
    auto measure = [&](const char* label) {
        double gap = 0, fps = 0;
        for (int i = 0; i < 2; ++i) {
            std::this_thread::sleep_for(1000ms);
            const auto s = cam->stats();
            gap = std::max(gap, s.maxGapMs);
            fps += s.fps / 2;
        }
        cam->requestAnalysis();
        webcam::FrameStats st;
        for (int i = 0; i < 50 && !cam->takeAnalysis(st); ++i)
            std::this_thread::sleep_for(20ms);
        const auto exp = find(webcam::CameraControl::Kind::Camera, 4); // CameraControl_Exposure
        luma::test::note(std::format("{:<22} fps {:.0f}, longest gap {:.1f} ms, median luma {:.2f}", label, fps, gap,
                                     st.p50));
    };
    measure("priority ON (original)");
    auto c = *prio;
    c.value = original ? 0 : 1;
    cam->setControl(c);
    std::this_thread::sleep_for(4000ms); // let auto exposure converge
    measure(c.value ? "priority ON" : "priority OFF");
    c.value = original; // restore
    cam->setControl(c);
    std::this_thread::sleep_for(500ms);
    auto after = find(webcam::CameraControl::Kind::ExposurePriority, 0);
    CHECK_MSG(after && after->value == original, "exposure priority was not restored");
    luma::test::note(std::format("restored exposure priority to {}", original));
    cam->stop();
}

namespace {

struct ProbeResult {
    std::string text;
    bool ok = false;
};

// Runs ffprobe (from the G: FFmpeg install) and returns its stdout.
ProbeResult ffprobe(const std::wstring& args)
{
    ProbeResult r;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    CreatePipe(&rd, &wr, &sa, 0);
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"G:\\DevTools\\ffmpeg\\bin\\ffprobe.exe\" -v error " + args;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(wr);
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0)
            r.text.append(buf, n);
        WaitForSingleObject(pi.hProcess, 30000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        r.ok = code == 0;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        CloseHandle(wr);
    }
    CloseHandle(rd);
    return r;
}

std::filesystem::path testFile(const char* name)
{
    return std::filesystem::path(L"G:\\LumaCapture\\build\\test-output") / name;
}

double durationOf(const std::filesystem::path& file, const wchar_t* streamSel)
{
    const auto r = ffprobe(std::wstring(L"-select_streams ") + streamSel +
                           L" -show_entries stream=duration:format=duration -of default=nw=1:nk=1 \"" + file.wstring() + L"\"");
    // First numeric line = stream duration (may be N/A in MKV), second = format duration.
    double v = 0;
    size_t pos = 0;
    while (pos < r.text.size()) {
        const size_t e = r.text.find('\n', pos);
        const std::string line = r.text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        try {
            v = std::stod(line);
            break;
        } catch (...) {
        }
        if (e == std::string::npos)
            break;
        pos = e + 1;
    }
    return v;
}

} // namespace

// Short full-pipeline recording: display 720p30 + system audio + mic + webcam
// overlay, with one pause. Verifies the file is finalised, decodable, has the
// expected streams and a duration that excludes the pause.
TEST("hw", recording_lifecycle_short)
{
    const auto file = testFile("lifecycle.mkv");
    std::error_code ec;
    std::filesystem::remove(file, ec);

    std::shared_ptr<webcam::WebcamCapture> cam;
    try {
        cam = std::make_shared<webcam::WebcamCapture>();
        cam->start(firstCamera(), webcam::WebcamMode{640, 480, 30, 1, ""});
    } catch (const std::exception& e) {
        luma::test::note(std::string("no webcam: ") + e.what());
        cam.reset();
    }

    session::SessionConfig cfg;
    cfg.source.kind = session::SourceKind::Display;
    cfg.source.outputIndex = 0;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.fps = 30;
    cfg.encoder.preset = "ultrafast";
    cfg.encoder.crf = 23;
    cfg.encoder.threads = 3;
    cfg.recordAudio = true;
    cfg.audio.system = true;
    cfg.audio.mic = true;
    cfg.audio.separateTracks = true;
    cfg.outputFile = file;
    if (cam) {
        cfg.webcamFrames = &cam->frames();
        cfg.composition.webcam.enabled = true;
        cfg.composition.webcam.x = 0.7f;
        cfg.composition.webcam.y = 0.7f;
        cfg.composition.webcam.w = 0.25f;
        cfg.composition.webcam.h = 0.25f;
        cfg.composition.webcam.shape = gpu::WebcamShape::Circle;
    }

    auto session = std::make_shared<session::RecordingSession>(cfg);
    double ms = 0;
    CHECK_MSG(finishesWithin(15s, [session] { session->start(); }, &ms), "start() hung");
    luma::test::note(std::format("start() {:.0f} ms", ms));
    std::this_thread::sleep_for(2500ms);
    session->pause();
    std::this_thread::sleep_for(1500ms);
    CHECK(session->paused());
    session->resume();
    std::this_thread::sleep_for(2500ms);
    const auto st = session->status();
    luma::test::note(std::format("status: {:.2f} s recorded, {} captured, {} dropped, {} bytes, audio '{}'",
                                 st.elapsedSeconds, st.capturedFrames, st.droppedFrames, st.bytes, st.audioStatus));
    CHECK_MSG(!st.failed, st.error);
    CHECK_MSG(finishesWithin(20s, [session] { session->stop(); }, &ms), "stop() hung");
    luma::test::note(std::format("stop() {:.0f} ms", ms));
    CHECK_MSG(finishesWithin(5s, [session] { session->stop(); }), "second stop() hung (must be idempotent)");
    CHECK_MSG(!session->failed(), session->errorMessage());
    if (cam)
        cam->stop();

    CHECK(std::filesystem::exists(file));
    CHECK_MSG(!std::filesystem::exists(std::filesystem::path(file.wstring() + L".lumarec")), "recovery marker left behind");
    const auto streams = ffprobe(L"-show_entries stream=index,codec_type,codec_name -of csv=p=0 \"" + file.wstring() + L"\"");
    std::string list = streams.text;
    list.erase(std::remove(list.begin(), list.end(), '\r'), list.end());
    std::replace(list.begin(), list.end(), '\n', ' ');
    luma::test::note("streams: " + list);
    CHECK(streams.ok);
    CHECK(streams.text.find("h264,video") != std::string::npos || streams.text.find("video") != std::string::npos);
    const auto decode = ffprobe(L"-count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 \"" +
                                file.wstring() + L"\"");
    CHECK_MSG(decode.ok, "video does not decode: " + decode.text);
    const double v = durationOf(file, L"v:0"), a = durationOf(file, L"a:0");
    luma::test::note(std::format("durations: video {:.2f} s, audio {:.2f} s (5.0 s expected, pause excluded)", v, a));
    CHECK_MSG(v > 4.3 && v < 5.8, "video duration should exclude the 1.5 s pause");
    CHECK_MSG(std::abs(v - a) < 0.25, "audio and video lengths differ by more than 250 ms");
}

// MKV -> MP4 stream copy of the recording made by recording_lifecycle_short.
TEST("hw", mp4_conversion)
{
    const auto mkv = testFile("lifecycle.mkv");
    if (!std::filesystem::exists(mkv)) {
        luma::test::note("lifecycle.mkv missing - run recording_lifecycle_short first");
        CHECK(false);
    }
    const auto mp4 = testFile("lifecycle.mp4");
    std::error_code ec;
    std::filesystem::remove(mp4, ec);
    mux::remux(mkv, mp4);
    const auto r = ffprobe(L"-show_entries stream=codec_type -of csv=p=0 \"" + mp4.wstring() + L"\"");
    CHECK(r.ok);
    const double d = durationOf(mp4, L"v:0");
    luma::test::note(std::format("mp4 video duration {:.2f} s", d));
    CHECK(d > 4.3 && d < 5.8);
}

// Simulates a crash: a child process records for 3 s and then terminates itself
// without finalising. The .lumarec marker must remain and the recovery remux
// must produce a decodable file.
TEST("hw", crash_recovery)
{
    const auto file = testFile("crashed.mkv");
    std::error_code ec;
    std::filesystem::remove(file, ec);
    std::filesystem::remove(std::filesystem::path(file.wstring() + L".lumarec"), ec);
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cmd = std::wstring(L"\"") + self + L"\" --child-crash-record \"" + file.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    CHECK(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    luma::test::note(std::format("child exit code {} (0xDEAD expected: killed while recording)", code));
    CHECK(code == 0xDEAD);
    CHECK_MSG(std::filesystem::exists(std::filesystem::path(file.wstring() + L".lumarec")), "recovery marker missing");
    const auto recovered = testFile("crashed-recovered.mkv");
    std::filesystem::remove(recovered, ec);
    mux::remux(file, recovered);
    const auto decode = ffprobe(L"-count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 \"" +
                                recovered.wstring() + L"\"");
    CHECK_MSG(decode.ok, decode.text);
    const int frames = std::atoi(decode.text.c_str());
    luma::test::note(std::format("recovered file decodes: {} video frames", frames));
    CHECK(frames > 30);
}

namespace {

// Runs an FFmpeg tool from the G: install and returns its stdout (binary-safe).
std::string runFfmpeg(const std::wstring& args)
{
    std::string out;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    CreatePipe(&rd, &wr, &sa, 0);
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = nul;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"G:\\DevTools\\ffmpeg\\bin\\ffmpeg.exe\" -v error " + args;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(wr);
        char buf[65536];
        DWORD n = 0;
        while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0)
            out.append(buf, n);
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        CloseHandle(wr);
    }
    CloseHandle(nul);
    CloseHandle(rd);
    return out;
}

struct Rgb {
    int r = 0, g = 0, b = 0;
};

// Mean colour of a w x h patch at (x, y) of frame `n` of a video file.
Rgb samplePatch(const std::filesystem::path& file, int n, int x, int y, int w, int h)
{
    const std::string raw = runFfmpeg(std::format(L"-i \"{}\" -vf \"select=eq(n\\,{}),crop={}:{}:{}:{}\" -frames:v 1 "
                                                  L"-f rawvideo -pix_fmt rgb24 -",
                                                  file.wstring(), n, w, h, x, y));
    Rgb m;
    const size_t px = raw.size() / 3;
    if (px == 0)
        return {-1, -1, -1};
    long long r = 0, g = 0, b = 0;
    for (size_t i = 0; i < px; ++i) {
        r += static_cast<unsigned char>(raw[i * 3]);
        g += static_cast<unsigned char>(raw[i * 3 + 1]);
        b += static_cast<unsigned char>(raw[i * 3 + 2]);
    }
    return {static_cast<int>(r / px), static_cast<int>(g / px), static_cast<int>(b / px)};
}

int colourDistance(Rgb a, COLORREF c)
{
    return std::abs(a.r - GetRValue(c)) + std::abs(a.g - GetGValue(c)) + std::abs(a.b - GetBValue(c));
}

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

// A solid-colour top-most test window of one of the kinds Qt/Win32 apps create.
struct ProbeWindow {
    enum class Kind { Normal, LayeredAlpha, LayeredPerPixel };
    const char* label;
    Kind kind;
    COLORREF colour;
    bool exclude;
    bool excludeBeforeShow;
    RECT rect{};
    HWND hwnd = nullptr;
    BOOL affinityOk = FALSE;
    DWORD affinityError = 0;
    DWORD affinityRead = 0;
};

LRESULT CALLBACK probeProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        HBRUSH br = CreateSolidBrush(static_cast<COLORREF>(GetWindowLongPtrW(h, GWLP_USERDATA)));
        FillRect(dc, &ps.rcPaint, br);
        DeleteObject(br);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

// Per-pixel alpha content via UpdateLayeredWindow (how Qt draws WA_TranslucentBackground windows).
void paintPerPixel(HWND h, const RECT& r, COLORREF c, BYTE alpha)
{
    const int w = r.right - r.left, ht = r.bottom - r.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -ht;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto* px = static_cast<uint32_t*>(bits);
    const uint32_t pr = GetRValue(c) * alpha / 255, pg = GetGValue(c) * alpha / 255, pb = GetBValue(c) * alpha / 255;
    for (int i = 0; i < w * ht; ++i)
        px[i] = (static_cast<uint32_t>(alpha) << 24) | (pr << 16) | (pg << 8) | pb; // premultiplied BGRA
    HGDIOBJ old = SelectObject(mem, bmp);
    POINT dst{r.left, r.top}, src{0, 0};
    SIZE size{w, ht};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(h, screen, &dst, &size, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void applyAffinity(ProbeWindow& p)
{
    p.affinityOk = SetWindowDisplayAffinity(p.hwnd, WDA_EXCLUDEFROMCAPTURE);
    p.affinityError = p.affinityOk ? 0 : GetLastError();
    GetWindowDisplayAffinity(p.hwnd, &p.affinityRead);
}

} // namespace

// Which kinds of windows does WDA_EXCLUDEFROMCAPTURE really keep out of a
// LumaCapture display recording (DXGI Desktop Duplication -> encoder -> MKV), and
// out of a GDI screenshot? Shows one test window of each kind, records 2.5 s,
// decodes a frame and checks whether each window's colour is in the file.
TEST("hw", capture_exclusion_in_recorded_file)
{
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::vector<ProbeWindow> probes = {
        {"normal window, excluded after show", ProbeWindow::Kind::Normal, RGB(255, 0, 255), true, false},
        {"layered (alpha) window, excluded after show", ProbeWindow::Kind::LayeredAlpha, RGB(0, 255, 255), true, false},
        {"per-pixel layered (Qt translucent), excluded after show", ProbeWindow::Kind::LayeredPerPixel, RGB(255, 255, 0), true, false},
        {"per-pixel layered (Qt translucent), excluded before show", ProbeWindow::Kind::LayeredPerPixel, RGB(40, 80, 255), true, true},
        {"control: normal window, NOT excluded", ProbeWindow::Kind::Normal, RGB(255, 128, 0), false, false},
    };
    for (size_t i = 0; i < probes.size(); ++i)
        probes[i].rect = RECT{100 + static_cast<LONG>(i) * 350, 260, 400 + static_cast<LONG>(i) * 350, 460};

    // The windows live on their own thread with a message loop (painting, DWM updates).
    std::atomic<bool> ready{false}, quit{false};
    std::thread ui([&] {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW wc{};
        wc.lpfnWndProc = probeProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"LumaCaptureProbe";
        RegisterClassW(&wc);
        for (ProbeWindow& p : probes) {
            DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
            if (p.kind != ProbeWindow::Kind::Normal)
                ex |= WS_EX_LAYERED;
            p.hwnd = CreateWindowExW(ex, wc.lpszClassName, L"probe", WS_POPUP, p.rect.left, p.rect.top,
                                     p.rect.right - p.rect.left, p.rect.bottom - p.rect.top, nullptr, nullptr,
                                     wc.hInstance, nullptr);
            SetWindowLongPtrW(p.hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(p.colour));
            if (p.kind == ProbeWindow::Kind::LayeredAlpha)
                SetLayeredWindowAttributes(p.hwnd, 0, 255, LWA_ALPHA);
            if (p.kind == ProbeWindow::Kind::LayeredPerPixel)
                paintPerPixel(p.hwnd, p.rect, p.colour, 235);
            if (p.exclude && p.excludeBeforeShow)
                applyAffinity(p);
            ShowWindow(p.hwnd, SW_SHOWNOACTIVATE);
            UpdateWindow(p.hwnd);
            if (p.exclude && !p.excludeBeforeShow)
                applyAffinity(p);
        }
        ready = true;
        MSG msg;
        while (!quit) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(10);
        }
        for (ProbeWindow& p : probes)
            DestroyWindow(p.hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
    });
    while (!ready)
        std::this_thread::sleep_for(10ms);
    std::this_thread::sleep_for(400ms); // let DWM compose them

    // GDI screenshot of the same area (the v1 screenshot path).
    const RECT row{probes.front().rect.left, probes.front().rect.top, probes.back().rect.right, probes.back().rect.bottom};
    std::vector<Rgb> gdi;
    try {
        const auto shot = capture::screenshotRect(row, false);
        for (const ProbeWindow& p : probes) {
            const int cx = (p.rect.left + p.rect.right) / 2 - row.left, cy = (p.rect.top + p.rect.bottom) / 2 - row.top;
            const uint32_t v = shot.pixels[static_cast<size_t>(cy) * shot.width + cx];
            gdi.push_back({static_cast<int>((v >> 16) & 255), static_cast<int>((v >> 8) & 255), static_cast<int>(v & 255)});
        }
    } catch (const std::exception& e) {
        luma::test::note(std::string("GDI screenshot failed: ") + e.what());
    }

    const auto file = testFile("exclusion.mkv");
    std::error_code ec;
    std::filesystem::remove(file, ec);
    session::SessionConfig cfg;
    cfg.source.kind = session::SourceKind::Display;
    cfg.source.outputIndex = 0;
    cfg.source.captureCursor = false;
    cfg.fps = 30;
    cfg.encoder.preset = "ultrafast";
    cfg.encoder.crf = 18;
    cfg.recordAudio = false;
    cfg.outputFile = file;
    {
        auto session = std::make_shared<session::RecordingSession>(cfg);
        session->start();
        std::this_thread::sleep_for(2500ms);
        session->stop();
        CHECK_MSG(!session->failed(), session->errorMessage());
    }
    quit = true;
    ui.join();

    bool controlSeen = false;
    for (size_t i = 0; i < probes.size(); ++i) {
        const ProbeWindow& p = probes[i];
        const int cx = (p.rect.left + p.rect.right) / 2 - 20, cy = (p.rect.top + p.rect.bottom) / 2 - 20;
        const Rgb rec = samplePatch(file, 45, cx, cy, 40, 40);
        const bool inVideo = rec.r >= 0 && colourDistance(rec, p.colour) < 60;
        const bool inGdi = i < gdi.size() && colourDistance(gdi[i], p.colour) < 30;
        if (!p.exclude)
            controlSeen = inVideo;
        luma::test::note(std::format("{:<58} affinity call {} (err {}, reads 0x{:X}) -> recording: {}, GDI screenshot: {}",
                                     p.label, p.exclude ? (p.affinityOk ? "OK" : "FAILED") : "n/a", p.affinityError,
                                     p.affinityRead, inVideo ? "VISIBLE" : "hidden", inGdi ? "VISIBLE" : "hidden"));
    }
    CHECK_MSG(controlSeen, "the non-excluded control window is not in the recording - the probe itself is broken");
}

TEST("hw", webcam_start_stop_cycles)
{
    std::string name;
    const std::wstring link = firstCamera(&name);
    luma::test::note("camera: " + name);
    // Varying run times so stop() lands at different points of the frame cycle.
    const int runMs[] = {1500, 700, 1033, 400, 1250, 90};
    for (int i = 0; i < static_cast<int>(std::size(runMs)); ++i) {
        auto cam = std::make_shared<webcam::WebcamCapture>();
        webcam::WebcamMode mode;
        mode.width = 1280;
        mode.height = 720;
        mode.fpsNum = 30;
        mode.fpsDen = 1;
        mode.format = "NV12";
        cam->start(link, mode);
        const auto t0 = std::chrono::steady_clock::now();
        while (cam->state() != webcam::WebcamCapture::State::Running && std::chrono::steady_clock::now() - t0 < 6s)
            std::this_thread::sleep_for(20ms);
        CHECK_MSG(cam->state() == webcam::WebcamCapture::State::Running, "camera did not start: " + cam->message());
        std::this_thread::sleep_for(std::chrono::milliseconds(runMs[i]));
        const uint64_t frames = cam->frames().sequence();
        double ms = 0;
        const bool ok = finishesWithin(6s, [cam] { cam->stop(); }, &ms);
        luma::test::note(std::format("cycle {}: ran {} ms, {} frames, stop() {}", i + 1, runMs[i], frames,
                                     ok ? std::format("took {:.0f} ms", ms) : std::string("DID NOT RETURN (hang)")));
        CHECK_MSG(ok, "WebcamCapture::stop() hung");
    }
}

