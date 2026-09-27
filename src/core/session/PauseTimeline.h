#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace luma::session {

// Records pause intervals on the QPC clock. Video and audio both derive their
// output timestamps from the same intervals (media time = wall time - paused
// time before it), which keeps them in sync across any number of pauses.
class PauseTimeline {
public:
    void pause(int64_t qpc)
    {
        std::lock_guard lock(m_mutex);
        if (m_currentStart < 0)
            m_currentStart = qpc;
    }

    void resume(int64_t qpc)
    {
        std::lock_guard lock(m_mutex);
        if (m_currentStart >= 0) {
            m_done.emplace_back(m_currentStart, std::max(qpc, m_currentStart));
            m_currentStart = -1;
        }
    }

    bool paused() const
    {
        std::lock_guard lock(m_mutex);
        return m_currentStart >= 0;
    }

    // True if wall time t lies inside a pause (finished or ongoing).
    bool isPausedAt(int64_t t) const
    {
        std::lock_guard lock(m_mutex);
        if (m_currentStart >= 0 && t >= m_currentStart)
            return true;
        for (const auto& [s, e] : m_done)
            if (t >= s && t < e)
                return true;
        return false;
    }

    // Total paused QPC ticks in intervals that ended at or before t.
    int64_t pausedBefore(int64_t t) const
    {
        std::lock_guard lock(m_mutex);
        int64_t sum = 0;
        for (const auto& [s, e] : m_done)
            if (e <= t)
                sum += e - s;
        return sum;
    }

    // Paused time up to `now`, including an ongoing pause.
    int64_t totalPaused(int64_t now) const
    {
        std::lock_guard lock(m_mutex);
        int64_t sum = 0;
        for (const auto& [s, e] : m_done)
            sum += e - s;
        if (m_currentStart >= 0 && now > m_currentStart)
            sum += now - m_currentStart;
        return sum;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::pair<int64_t, int64_t>> m_done;
    int64_t m_currentStart = -1;
};

} // namespace luma::session
