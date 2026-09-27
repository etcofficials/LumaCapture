#pragma once

#include "encode/FfmpegUtil.h"

#include <atomic>
#include <memory>
#include <vector>

namespace luma::encode {

// A fixed set of preallocated NV12 AVFrames shared by the capture and encoder
// threads. Nothing is allocated while recording: a frame is "free" when no
// queue entry references it and FFmpeg holds no reference to its buffers.
// A single frame may be referenced several times (repeated static frames).
class FramePool {
public:
    FramePool(int width, int height, unsigned count);

    // Reserves a free frame (reference count 1). Returns -1 when all are in use.
    int acquire();
    void addRef(int slot) { m_slots[slot]->refs.fetch_add(1, std::memory_order_relaxed); }
    void release(int slot) { m_slots[slot]->refs.fetch_sub(1, std::memory_order_acq_rel); }

    AVFrame* frame(int slot) const { return m_slots[slot]->frame.get(); }
    unsigned size() const { return static_cast<unsigned>(m_slots.size()); }
    size_t bytesPerFrame() const { return m_bytesPerFrame; }

private:
    struct Slot {
        ff::FramePtr frame;
        std::atomic<int> refs{0};
    };
    std::vector<std::unique_ptr<Slot>> m_slots;
    size_t m_bytesPerFrame = 0;
};

} // namespace luma::encode
