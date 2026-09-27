#include "gpu/CursorOverlay.h"

#include "util/HResult.h"
#include "util/Log.h"

#include <algorithm>

namespace luma::gpu {

CursorOverlay::CursorOverlay(ID3D11Device* device)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kMaxSize;
    desc.Height = kMaxSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    check(device->CreateTexture2D(&desc, nullptr, &m_color), "CreateTexture2D(cursor colour)");
    check(device->CreateShaderResourceView(m_color.Get(), nullptr, &m_colorSrv), "CreateSRV(cursor colour)");

    desc.Format = DXGI_FORMAT_R8_UNORM;
    check(device->CreateTexture2D(&desc, nullptr, &m_invert), "CreateTexture2D(cursor invert)");
    check(device->CreateShaderResourceView(m_invert.Get(), nullptr, &m_invertSrv), "CreateSRV(cursor invert)");
}

void CursorOverlay::upload(ID3D11DeviceContext* ctx, const capture::CursorImage& image)
{
    if (image.width > kMaxSize || image.height > kMaxSize)
        log::warn("Cursor {}x{} larger than {}px; clipping", image.width, image.height, kMaxSize);
    m_width = std::min(image.width, kMaxSize);
    m_height = std::min(image.height, kMaxSize);
    if (m_width == 0 || m_height == 0)
        return;

    const D3D11_BOX box{0, 0, 0, m_width, m_height, 1};
    ctx->UpdateSubresource(m_color.Get(), 0, &box, image.bgra.data(), image.width * 4, 0);
    ctx->UpdateSubresource(m_invert.Get(), 0, &box, image.invert.data(), image.width, 0);
}

} // namespace luma::gpu
