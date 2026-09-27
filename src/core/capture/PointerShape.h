#pragma once

#include <dxgi1_2.h>

#include <cstdint>
#include <vector>

namespace luma::capture {

// Cursor bitmap decoded into a form the GPU shader can composite:
// straight-alpha BGRA colour + a per-pixel "invert what is underneath" mask.
struct CursorImage {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint32_t> bgra;  // width * height, 0xAARRGGBB little-endian = B,G,R,A bytes
    std::vector<uint8_t> invert; // width * height, 0 or 255
};

// Decodes the three Desktop Duplication pointer shape types.
// MASKED_COLOR XOR pixels are approximated: XOR with black is transparent,
// XOR with any other colour inverts the pixel (the common "I-beam" case).
// Reuses the vectors in `out`, so steady-state decoding does not allocate.
void decodePointerShape(const DXGI_OUTDUPL_POINTER_SHAPE_INFO& info, const uint8_t* data, CursorImage& out);

} // namespace luma::capture
