#include "gpu/ReadbackRing.h"

#include "util/HResult.h"
#include "util/QpcClock.h"

#include <stdexcept>

namespace luma::gpu {

ReadbackRing::ReadbackRing(ID3D11Device* device, const std::vector<ID3D11Texture2D*>& planes, unsigned slots)
    : m_slots(slots), m_planes(static_cast<unsigned>(planes.size()))
{
    if (planes.empty() || planes.size() > kMaxPlanes || slots < 2)
        throw std::invalid_argument("ReadbackRing: need 1..2 planes and at least 2 slots");

    for (Slot& slot : m_slots) {
        for (unsigned p = 0; p < m_planes; ++p) {
            D3D11_TEXTURE2D_DESC desc{};
            planes[p]->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            check(device->CreateTexture2D(&desc, nullptr, &slot.tex[p]), "CreateTexture2D(staging)");
        }
    }
}

void ReadbackRing::submit(ID3D11DeviceContext* ctx, const std::vector<ID3D11Texture2D*>& planes)
{
    if (full())
        throw std::logic_error("ReadbackRing::submit on a full ring");
    Slot& slot = m_slots[(m_head + m_count) % m_slots.size()];
    for (unsigned p = 0; p < m_planes; ++p)
        ctx->CopyResource(slot.tex[p].Get(), planes[p]);
    ++m_count;
}

ReadbackRing::MapResult ReadbackRing::mapOldest(ID3D11DeviceContext* ctx, bool wait, Mapped& out)
{
    MapResult result;
    if (m_count == 0)
        return result;
    Slot& slot = m_slots[m_head];

    for (unsigned p = 0; p < m_planes; ++p) {
        D3D11_MAPPED_SUBRESOURCE ms{};
        HRESULT hr = ctx->Map(slot.tex[p].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &ms);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
            if (!wait) {
                for (unsigned q = 0; q < p; ++q)
                    ctx->Unmap(slot.tex[q].Get(), 0);
                return result;
            }
            const int64_t t0 = qpc::now();
            hr = ctx->Map(slot.tex[p].Get(), 0, D3D11_MAP_READ, 0, &ms);
            result.stalled = true;
            result.stallMs += qpc::toMs(qpc::now() - t0);
        }
        if (FAILED(hr)) {
            for (unsigned q = 0; q < p; ++q)
                ctx->Unmap(slot.tex[q].Get(), 0);
            check(hr, "Map(staging)");
        }
        out.data[p] = static_cast<const uint8_t*>(ms.pData);
        out.pitch[p] = ms.RowPitch;
    }
    result.mapped = true;
    return result;
}

void ReadbackRing::unmapOldest(ID3D11DeviceContext* ctx)
{
    Slot& slot = m_slots[m_head];
    for (unsigned p = 0; p < m_planes; ++p)
        ctx->Unmap(slot.tex[p].Get(), 0);
    m_head = (m_head + 1) % m_slots.size();
    --m_count;
}

} // namespace luma::gpu
