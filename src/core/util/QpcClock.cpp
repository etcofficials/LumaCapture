#include "util/QpcClock.h"

#include <windows.h>

namespace luma::qpc {

int64_t now() noexcept
{
    LARGE_INTEGER v;
    QueryPerformanceCounter(&v);
    return v.QuadPart;
}

int64_t frequency() noexcept
{
    static const int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return freq;
}

} // namespace luma::qpc
