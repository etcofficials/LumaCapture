#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <vector>

namespace luma::gpu {

using Microsoft::WRL::ComPtr;

// A ring of STAGING textures used to read GPU results back to the CPU without
// stalling: frame N is copied into slot N % size and mapped only later, once
// the GPU has finished. Each slot holds one staging texture per plane.
class ReadbackRing {
public:
    static constexpr unsigned kMaxPlanes = 2;

    struct Mapped {
        std::array<const uint8_t*, kMaxPlanes> data{};
        std::array<unsigned, kMaxPlanes> pitch{};
    };

    struct MapResult {
        bool mapped = false;   // true -> Mapped is valid, call unmapOldest() afterwards
        bool stalled = false;  // GPU was not finished and we had to block
        double stallMs = 0;    // time spent blocked in Map
    };

    // `planes` are the source textures whose shape (size/format) each slot mirrors.
    ReadbackRing(ID3D11Device* device, const std::vector<ID3D11Texture2D*>& planes, unsigned slots);

    unsigned size() const { return static_cast<unsigned>(m_slots.size()); }
    unsigned inFlight() const { return m_count; }
    bool full() const { return m_count == m_slots.size(); }

    // Queues GPU copies of `planes` into the next free slot. Requires !full().
    void submit(ID3D11DeviceContext* ctx, const std::vector<ID3D11Texture2D*>& planes);

    // Maps the oldest in-flight slot. With wait=false it returns mapped=false if
    // the GPU is still working on it; with wait=true it blocks (a "stall").
    // Throws HResultError on real failures (including device loss).
    MapResult mapOldest(ID3D11DeviceContext* ctx, bool wait, Mapped& out);
    void unmapOldest(ID3D11DeviceContext* ctx);

    // Forgets all in-flight copies (after device loss or reset).
    void clear() { m_head = m_count = 0; }

private:
    struct Slot {
        std::array<ComPtr<ID3D11Texture2D>, kMaxPlanes> tex;
    };
    std::vector<Slot> m_slots;
    unsigned m_planes = 0;
    unsigned m_head = 0;  // oldest in-flight slot
    unsigned m_count = 0; // in-flight slots
};

} // namespace luma::gpu
