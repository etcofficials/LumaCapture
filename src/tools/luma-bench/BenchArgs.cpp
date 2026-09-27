#include "BenchArgs.h"

#include <cstdio>
#include <cwctype>
#include <string_view>

namespace luma::bench {
namespace {

std::string narrow(std::wstring_view w)
{
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w)
        s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

bool parseInt(std::wstring_view v, int& out)
{
    try {
        size_t pos = 0;
        const int value = std::stoi(std::wstring(v), &pos);
        if (pos != v.size())
            return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseDouble(std::wstring_view v, double& out)
{
    try {
        size_t pos = 0;
        const double value = std::stod(std::wstring(v), &pos);
        if (pos != v.size())
            return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

void printUsage()
{
    std::printf(
        "luma-bench - LumaCapture recording pipeline benchmark\n"
        "  Desktop Duplication -> GPU BGRA->NV12 -> staging ring -> bounded queue -> libx264 -> MKV\n\n"
        "Usage: luma-bench.exe [options]\n"
        "  --resolution WxH     encoded size, desktop is scaled to it   (default 1920x1080)\n"
        "  --fps N              capture/encode frame rate               (default 30)\n"
        "  --preset NAME        x264 preset: ultrafast|superfast|veryfast|faster|fast|medium (default superfast)\n"
        "  --crf N              x264 CRF                                 (default 23)\n"
        "  --keyint SECONDS     keyframe interval                       (default 2)\n"
        "  --threads N          x264 threads                            (default 3)\n"
        "  --x264-params S      extra x264 options, key=value:key=value\n"
        "  --duration SECONDS   benchmark length                        (default 60)\n"
        "  --cpu-convert        BGRA->NV12 + scaling on the CPU (libswscale) instead of the GPU shader\n"
        "  --queue N            bounded encoder queue capacity (frames)  (default 8)\n"
        "  --ring N             GPU readback staging slots               (default 3)\n"
        "  --output-index N     monitor to capture (see --list-outputs)  (default 0)\n"
        "  --out-dir PATH       recording folder (default G:\\video\\LumaCapture-Bench)\n"
        "  --report-interval S  seconds between live status lines        (default 2)\n"
        "  --csv PATH           append a machine-readable result row to PATH\n"
        "  --label TEXT         run label stored in the CSV row\n"
        "  --list-outputs       list monitors and exit\n"
        "  --verbose            debug logging\n"
        "  --help\n\n"
        "Example: luma-bench.exe --resolution 1920x1080 --fps 30 --preset superfast --duration 60\n");
}

std::optional<BenchArgs> parseArgs(int argc, wchar_t** argv)
{
    BenchArgs a;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg = argv[i];
        auto value = [&](std::wstring_view& out) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing value for %s\n", narrow(arg).c_str());
                return false;
            }
            out = argv[++i];
            return true;
        };
        auto bad = [&](std::wstring_view v) {
            std::fprintf(stderr, "Invalid value '%s' for %s\n", narrow(v).c_str(), narrow(arg).c_str());
            return std::nullopt;
        };

        std::wstring_view v;
        int n = 0;
        if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            printUsage();
            return std::nullopt;
        } else if (arg == L"--cpu-convert") {
            a.cpuConvert = true;
        } else if (arg == L"--list-outputs") {
            a.listOutputs = true;
        } else if (arg == L"--verbose") {
            a.verbose = true;
        } else if (arg == L"--resolution") {
            if (!value(v))
                return std::nullopt;
            const size_t x = v.find_first_of(L"xX");
            int w = 0, h = 0;
            if (x == std::wstring_view::npos || !parseInt(v.substr(0, x), w) || !parseInt(v.substr(x + 1), h) ||
                w < 64 || h < 64 || w % 2 || h % 2)
                return bad(v);
            a.width = w;
            a.height = h;
        } else if (arg == L"--fps") {
            if (!value(v) || !parseInt(v, n) || n < 1 || n > 240)
                return bad(v);
            a.fps = n;
        } else if (arg == L"--preset") {
            if (!value(v) || v.empty())
                return bad(v);
            a.preset = narrow(v);
        } else if (arg == L"--crf") {
            if (!value(v) || !parseInt(v, n) || n < 0 || n > 51)
                return bad(v);
            a.crf = n;
        } else if (arg == L"--keyint") {
            if (!value(v) || !parseDouble(v, a.keyintSeconds) || a.keyintSeconds <= 0)
                return bad(v);
        } else if (arg == L"--threads") {
            if (!value(v) || !parseInt(v, n) || n < 0 || n > 64)
                return bad(v);
            a.threads = n;
        } else if (arg == L"--x264-params") {
            if (!value(v))
                return std::nullopt;
            a.x264Params = narrow(v);
        } else if (arg == L"--duration") {
            if (!value(v) || !parseDouble(v, a.durationSeconds) || a.durationSeconds <= 0)
                return bad(v);
        } else if (arg == L"--queue") {
            if (!value(v) || !parseInt(v, n) || n < 1 || n > 256)
                return bad(v);
            a.queueCapacity = static_cast<unsigned>(n);
        } else if (arg == L"--ring") {
            if (!value(v) || !parseInt(v, n) || n < 2 || n > 16)
                return bad(v);
            a.readbackSlots = static_cast<unsigned>(n);
        } else if (arg == L"--output-index") {
            if (!value(v) || !parseInt(v, n) || n < 0)
                return bad(v);
            a.outputIndex = static_cast<unsigned>(n);
        } else if (arg == L"--out-dir") {
            if (!value(v) || v.empty())
                return bad(v);
            a.outDir = std::filesystem::path(v);
        } else if (arg == L"--csv") {
            if (!value(v) || v.empty())
                return bad(v);
            a.csvPath = std::filesystem::path(v);
        } else if (arg == L"--label") {
            if (!value(v))
                return std::nullopt;
            a.label = narrow(v);
        } else if (arg == L"--report-interval") {
            if (!value(v) || !parseDouble(v, a.reportIntervalSeconds) || a.reportIntervalSeconds < 0.5)
                return bad(v);
        } else {
            std::fprintf(stderr, "Unknown option %s (use --help)\n", narrow(arg).c_str());
            return std::nullopt;
        }
    }
    return a;
}

} // namespace luma::bench
