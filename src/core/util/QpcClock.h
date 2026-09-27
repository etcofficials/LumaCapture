#pragma once

#include <cstdint>

// QueryPerformanceCounter is the single master clock for the whole pipeline:
// capture ticks, audio (later) and all latency measurements use it.
namespace luma::qpc {

int64_t now() noexcept;
int64_t frequency() noexcept;

inline double toSeconds(int64_t ticks) noexcept { return static_cast<double>(ticks) / static_cast<double>(frequency()); }
inline double toMs(int64_t ticks) noexcept { return toSeconds(ticks) * 1000.0; }
inline int64_t fromSeconds(double s) noexcept { return static_cast<int64_t>(s * static_cast<double>(frequency())); }

} // namespace luma::qpc
