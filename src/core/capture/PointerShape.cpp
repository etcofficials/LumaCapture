#include "capture/PointerShape.h"

#include <cstring>

namespace luma::capture {
namespace {

constexpr uint32_t kBlack = 0xFF000000u;
constexpr uint32_t kWhite = 0xFFFFFFFFu;

void decodeMonochrome(const DXGI_OUTDUPL_POINTER_SHAPE_INFO& info, const uint8_t* data, CursorImage& out)
{
    // 1 bpp: AND mask in the top half, XOR mask in the bottom half.
    const unsigned h = info.Height / 2;
    out.width = info.Width;
    out.height = h;
    out.bgra.assign(static_cast<size_t>(out.width) * h, 0);
    out.invert.assign(static_cast<size_t>(out.width) * h, 0);
    for (unsigned y = 0; y < h; ++y) {
        const uint8_t* andRow = data + static_cast<size_t>(y) * info.Pitch;
        const uint8_t* xorRow = data + static_cast<size_t>(y + h) * info.Pitch;
        for (unsigned x = 0; x < out.width; ++x) {
            const uint8_t bit = static_cast<uint8_t>(0x80 >> (x & 7));
            const bool andBit = (andRow[x / 8] & bit) != 0;
            const bool xorBit = (xorRow[x / 8] & bit) != 0;
            const size_t i = static_cast<size_t>(y) * out.width + x;
            if (!andBit)
                out.bgra[i] = xorBit ? kWhite : kBlack; // opaque
            else if (xorBit)
                out.invert[i] = 255;                    // screen XOR 1 = invert
            // AND=1, XOR=0: transparent
        }
    }
}

void decodeColor(const DXGI_OUTDUPL_POINTER_SHAPE_INFO& info, const uint8_t* data, CursorImage& out)
{
    out.width = info.Width;
    out.height = info.Height;
    out.bgra.resize(static_cast<size_t>(out.width) * out.height);
    out.invert.assign(static_cast<size_t>(out.width) * out.height, 0);
    for (unsigned y = 0; y < out.height; ++y)
        std::memcpy(&out.bgra[static_cast<size_t>(y) * out.width], data + static_cast<size_t>(y) * info.Pitch,
                    static_cast<size_t>(out.width) * 4);
}

void decodeMaskedColor(const DXGI_OUTDUPL_POINTER_SHAPE_INFO& info, const uint8_t* data, CursorImage& out)
{
    // Alpha byte is a mask: 0 = replace with RGB, 0xFF = XOR RGB with the screen.
    out.width = info.Width;
    out.height = info.Height;
    out.bgra.assign(static_cast<size_t>(out.width) * out.height, 0);
    out.invert.assign(static_cast<size_t>(out.width) * out.height, 0);
    for (unsigned y = 0; y < out.height; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(data + static_cast<size_t>(y) * info.Pitch);
        for (unsigned x = 0; x < out.width; ++x) {
            const uint32_t px = row[x];
            const uint32_t rgb = px & 0x00FFFFFFu;
            const size_t i = static_cast<size_t>(y) * out.width + x;
            if ((px >> 24) == 0)
                out.bgra[i] = rgb | 0xFF000000u;
            else if (rgb != 0)
                out.invert[i] = 255;
        }
    }
}

} // namespace

void decodePointerShape(const DXGI_OUTDUPL_POINTER_SHAPE_INFO& info, const uint8_t* data, CursorImage& out)
{
    switch (info.Type) {
    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:   decodeMonochrome(info, data, out); break;
    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:        decodeColor(info, data, out); break;
    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR: decodeMaskedColor(info, data, out); break;
    default:
        out.width = out.height = 0;
        break;
    }
}

} // namespace luma::capture
