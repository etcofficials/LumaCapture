#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>

namespace luma::session {

// Fixed-capacity FIFO. The producer never blocks (tryPush fails when full);
// the consumer blocks in pop() until an item arrives or the queue is closed.
// Storage is allocated once in the constructor.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity) : m_items(capacity) {}

    size_t capacity() const { return m_items.size(); }

    bool tryPush(const T& item)
    {
        {
            std::lock_guard lock(m_mutex);
            if (m_closed || m_count == m_items.size())
                return false;
            m_items[(m_head + m_count) % m_items.size()] = item;
            ++m_count;
        }
        m_cv.notify_one();
        return true;
    }

    // Returns false once the queue is closed and empty.
    bool pop(T& out)
    {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [&] { return m_count > 0 || m_closed; });
        if (m_count == 0)
            return false;
        out = m_items[m_head];
        m_head = (m_head + 1) % m_items.size();
        --m_count;
        return true;
    }

    void close()
    {
        {
            std::lock_guard lock(m_mutex);
            m_closed = true;
        }
        m_cv.notify_all();
    }

    size_t size() const
    {
        std::lock_guard lock(m_mutex);
        return m_count;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<T> m_items;
    size_t m_head = 0;
    size_t m_count = 0;
    bool m_closed = false;
};

} // namespace luma::session
