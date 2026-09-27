#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace luma::bench {

struct BenchArgs {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    std::string preset = "superfast";
    int crf = 23;
    double keyintSeconds = 2.0;
    int threads = 3;
    std::string x264Params;
    double durationSeconds = 60;
    bool cpuConvert = false;
    unsigned queueCapacity = 8;
    unsigned readbackSlots = 3;
    unsigned outputIndex = 0;
    std::filesystem::path outDir = L"G:\\video\\LumaCapture-Bench";
    double reportIntervalSeconds = 2;
    std::filesystem::path csvPath; // append one result row per run when set
    std::string label;             // free-form run label written to the CSV
    bool listOutputs = false;
    bool verbose = false;
};

void printUsage();
// Returns nothing (after printing the reason) when the arguments are invalid or --help was given.
std::optional<BenchArgs> parseArgs(int argc, wchar_t** argv);

} // namespace luma::bench
