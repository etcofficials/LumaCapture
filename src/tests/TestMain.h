#pragma once

// Minimal self-contained test harness (no external framework downloads).

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace luma::test {

struct TestCase {
    const char* group; // "unit" or "hw" (needs real devices)
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry();
struct Registrar {
    Registrar(const char* group, const char* name, std::function<void()> fn)
    {
        registry().push_back({group, name, std::move(fn)});
    }
};

struct Failure {
    std::string message;
};

void fail(const char* file, int line, const std::string& msg);
void note(const std::string& msg); // extra line printed under the test result

} // namespace luma::test

#define LT_CAT2(a, b) a##b
#define LT_CAT(a, b) LT_CAT2(a, b)
#define TEST(group, name)                                                                                     \
    static void LT_CAT(test_, name)();                                                                        \
    static ::luma::test::Registrar LT_CAT(reg_, name)(group, #name, &LT_CAT(test_, name));                    \
    static void LT_CAT(test_, name)()
#define CHECK(cond)                                                                                           \
    do {                                                                                                      \
        if (!(cond))                                                                                          \
            ::luma::test::fail(__FILE__, __LINE__, "CHECK(" #cond ")");                                       \
    } while (0)
#define CHECK_MSG(cond, msg)                                                                                  \
    do {                                                                                                      \
        if (!(cond))                                                                                          \
            ::luma::test::fail(__FILE__, __LINE__, std::string("CHECK(" #cond "): ") + (msg));                \
    } while (0)
