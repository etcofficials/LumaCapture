// luma-tests: unit tests (pure logic) and opt-in hardware integration tests.
//   luma-tests.exe                 run unit tests
//   luma-tests.exe --hw            also run hardware tests (webcam, audio, short recordings)
//   luma-tests.exe --only <name>   run a single test
// Hardware tests write only into G:\LumaCapture\build\test-output.

#include "TestMain.h"

#include "util/Log.h"

#include <windows.h>

#include <chrono>
#include <cstring>
#include <string>

namespace luma::test {

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

namespace {
std::vector<std::string>* g_notes = nullptr;
}

void fail(const char* file, int line, const std::string& msg)
{
    const char* base = std::strrchr(file, '\\');
    throw Failure{std::string(base ? base + 1 : file) + ":" + std::to_string(line) + " " + msg};
}

void note(const std::string& msg)
{
    if (g_notes)
        g_notes->push_back(msg);
}

} // namespace luma::test

int childCrashRecord(const std::wstring& file);

int main(int argc, char** argv)
{
    using namespace luma::test;
    if (argc == 3 && std::strcmp(argv[1], "--child-crash-record") == 0) {
        std::wstring f;
        for (const char* c = argv[2]; *c; ++c)
            f.push_back(static_cast<unsigned char>(*c));
        return childCrashRecord(f);
    }
    bool hw = false;
    std::string only;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hw") == 0)
            hw = true;
        else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc)
            only = argv[++i];
    }
    CreateDirectoryW(L"G:\\LumaCapture\\build\\test-output", nullptr);
    luma::log::openFile(L"G:\\LumaCapture\\build\\test-output\\luma-tests.log");
    luma::log::setMinLevel(luma::log::Level::Warn); // keep console output readable

    int passed = 0, failed = 0, skipped = 0;
    for (const TestCase& t : registry()) {
        if (!only.empty() && only != t.name)
            continue;
        if (std::strcmp(t.group, "hw") == 0 && !hw && only.empty()) {
            ++skipped;
            continue;
        }
        std::vector<std::string> notes;
        g_notes = &notes;
        const auto t0 = std::chrono::steady_clock::now();
        std::string error;
        try {
            t.fn();
        } catch (const Failure& f) {
            error = f.message;
        } catch (const std::exception& e) {
            error = std::string("exception: ") + e.what();
        } catch (...) {
            error = "unknown exception";
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("[%s] %-4s %-40s %8.1f ms\n", error.empty() ? "PASS" : "FAIL", t.group, t.name, ms);
        for (const auto& n : notes)
            std::printf("            %s\n", n.c_str());
        if (!error.empty())
            std::printf("            -> %s\n", error.c_str());
        std::fflush(stdout);
        (error.empty() ? passed : failed)++;
        g_notes = nullptr;
    }
    std::printf("\n%d passed, %d failed, %d skipped%s\n", passed, failed, skipped,
                skipped ? " (hardware tests need --hw)" : "");
    return failed == 0 ? 0 : 1;
}
