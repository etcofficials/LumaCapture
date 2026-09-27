// luma-bench: runs the real LumaCapture recording pipeline (RecordingSession)
// for a fixed time and reports measured performance.

#include "BenchArgs.h"
#include "BenchReport.h"

#include "gpu/D3D11Device.h"
#include "session/RecordingSession.h"
#include "util/Log.h"
#include "util/QpcClock.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <system_error>
#include <thread>

using namespace luma;

namespace {

std::atomic<bool> g_interrupted{false};

BOOL WINAPI onConsoleCtrl(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_interrupted = true;
        return TRUE;
    }
    return FALSE;
}

std::string narrow(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w)
        s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

std::string cpuName()
{
    wchar_t buf[256] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        return narrow(buf);
    return "unknown CPU";
}

std::string timestamp()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

int listOutputs()
{
    const auto outputs = gpu::enumerateOutputs();
    for (size_t i = 0; i < outputs.size(); ++i) {
        const auto& o = outputs[i];
        std::printf("[%zu] %s  %ldx%ld @ %.0f Hz  on %s\n", i, narrow(o.deviceName).c_str(),
                    o.desktopRect.right - o.desktopRect.left, o.desktopRect.bottom - o.desktopRect.top, o.refreshHz,
                    narrow(o.adapterName).c_str());
    }
    return outputs.empty() ? 1 : 0;
}

int run(const bench::BenchArgs& args)
{
    // Reject C: explicitly - benchmark recordings must not fill the system SSD.
    const std::filesystem::path outDir = std::filesystem::absolute(args.outDir);
    const std::wstring root = outDir.root_name().wstring();
    if (root.size() >= 1 && (root[0] == L'C' || root[0] == L'c')) {
        log::error("Refusing to write recordings to C: ({}); choose another --out-dir", outDir.string());
        return 2;
    }
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        log::error("Cannot create output directory {}: {}", outDir.string(), ec.message());
        return 2;
    }

    const std::string base = std::format("bench_{}_{}x{}_{}fps_{}_crf{}_t{}{}", timestamp(), args.width, args.height,
                                         args.fps, args.preset, args.crf, args.threads, args.cpuConvert ? "_cpuconv" : "");
    const auto file = outDir / (base + ".mkv");
    log::openFile(outDir / (base + ".log"));

    session::SessionConfig cfg;
    cfg.source.kind = session::SourceKind::Display;
    cfg.source.outputIndex = args.outputIndex;
    cfg.recordAudio = false; // video pipeline benchmark
    cfg.width = args.width;
    cfg.height = args.height;
    cfg.fps = args.fps;
    cfg.cpuConvert = args.cpuConvert;
    cfg.queueCapacity = args.queueCapacity;
    cfg.readbackSlots = args.readbackSlots;
    cfg.encoder.preset = args.preset;
    cfg.encoder.crf = args.crf;
    cfg.encoder.keyframeSeconds = args.keyintSeconds;
    cfg.encoder.threads = args.threads;
    cfg.encoder.x264Params = args.x264Params;
    cfg.outputFile = file;
    cfg.expectedFrames = static_cast<unsigned>(args.durationSeconds * args.fps) + 64;

    session::RecordingSession session(cfg);
    try {
        session.start();
    } catch (const std::exception& e) {
        log::error("Failed to start recording pipeline: {}", e.what());
        return 3;
    }

    const auto& info = session.info();
    log::info("CPU: {}", cpuName());
    log::info("Display: {}x{} @ {:.0f}Hz ({}, {})", info.desktopWidth, info.desktopHeight, info.refreshHz,
              narrow(info.outputName), narrow(info.adapterName));
    log::info("D3D feature level: {}_{}", (info.featureLevel >> 12) & 0xF, (info.featureLevel >> 8) & 0xF);
    log::info("Capture: Desktop Duplication (IDXGIOutput1::DuplicateOutput), cursor composited from pointer shape");
    log::info("Conversion: {}", args.cpuConvert ? "CPU libswscale BGRA->NV12 (GPU composites cursor only)"
                                                : "GPU NV12 shader (BT.709 limited)");
    log::info("Readback ring: {} staging slots | Queue: {} frames | Frame pool: {} x NV12 = {:.1f} MB",
              args.readbackSlots, args.queueCapacity, info.framePoolSize, info.framePoolBytes / 1048576.0);
    log::info("Encoder: {} (software; no hardware encoder is used)", info.encoderDescription);
    log::info("Preset: {}", args.preset);
    log::info("Threads: {}", args.threads);
    log::info("Target: {}x{} @ {} FPS for {:.0f} s", args.width, args.height, args.fps, args.durationSeconds);
    log::info("Output: {}", file.string());

    // The monitor thread only wakes once per second; high priority keeps its
    // sampling windows accurate when the CPU is saturated.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    bench::BenchMonitor monitor(session);
    const int64_t start = session.startQpc();
    const int64_t end = start + qpc::fromSeconds(args.durationSeconds);
    int64_t nextSample = start + qpc::frequency();
    double nextPrint = args.reportIntervalSeconds;

    while (!g_interrupted && !session.failed()) {
        const int64_t now = qpc::now();
        if (now >= end)
            break;
        const int64_t wake = std::min(nextSample, end);
        if (wake > now)
            std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(qpc::toMs(wake - now) * 1000)));
        if (qpc::now() >= nextSample) {
            const auto& s = monitor.sample();
            nextSample += qpc::frequency();
            if (s.elapsedSeconds + 0.05 >= nextPrint) {
                monitor.printLive(s);
                nextPrint += args.reportIntervalSeconds;
            }
        }
    }
    if (g_interrupted)
        log::warn("Interrupted by user; stopping early");

    bench::RunResult result;
    result.wallSeconds = qpc::toSeconds(qpc::now() - start);
    log::info("Stopping: draining queue and flushing encoder...");
    session.stop();
    result.failed = session.failed();
    result.error = session.errorMessage();
    result.fileBytes = std::filesystem::file_size(file, ec);
    if (ec)
        result.fileBytes = 0;

    bench::printSummary(args, session, monitor, result, file);
    log::closeFile();
    return result.failed ? 4 : 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    // Desktop Duplication reports physical pixels only when the process is DPI aware.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);

    const auto args = bench::parseArgs(argc, argv);
    if (!args)
        return 1;
    if (args->verbose)
        log::setMinLevel(log::Level::Debug);

    try {
        if (args->listOutputs)
            return listOutputs();
        return run(*args);
    } catch (const std::exception& e) {
        log::error("Fatal: {}", e.what());
        return 5;
    }
}
