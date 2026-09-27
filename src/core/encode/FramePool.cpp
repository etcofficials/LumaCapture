#include "encode/FramePool.h"

extern "C" {
#include <libavutil/imgutils.h>
}

namespace luma::encode {

FramePool::FramePool(int width, int height, unsigned count)
{
    m_slots.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        auto slot = std::make_unique<Slot>();
        slot->frame.reset(av_frame_alloc());
        if (!slot->frame)
            throw std::bad_alloc();
        AVFrame* f = slot->frame.get();
        f->format = AV_PIX_FMT_NV12;
        f->width = width;
        f->height = height;
        f->color_range = AVCOL_RANGE_MPEG;
        f->colorspace = AVCOL_SPC_BT709;
        f->color_primaries = AVCOL_PRI_BT709;
        f->color_trc = AVCOL_TRC_BT709;
        ff::check(av_frame_get_buffer(f, 0), "av_frame_get_buffer");
        m_slots.push_back(std::move(slot));
    }
    m_bytesPerFrame = static_cast<size_t>(av_image_get_buffer_size(AV_PIX_FMT_NV12, width, height, 1));
}

int FramePool::acquire()
{
    for (size_t i = 0; i < m_slots.size(); ++i) {
        Slot& s = *m_slots[i];
        int expected = 0;
        if (s.refs.load(std::memory_order_acquire) != 0 || !av_frame_is_writable(s.frame.get()))
            continue;
        if (s.refs.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace luma::encode
