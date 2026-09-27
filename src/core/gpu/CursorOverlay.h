#pragma once

#include "capture/PointerShape.h"

#include <d3d11.h>
#include <wrl/client.h>

namespace luma::gpu {

using Microsoft::WRL::ComPtr;

struct CursorPlacement {
    bool visible = false;
    int x = 0; // bitmap top-left in source pixels
    int y = 0;
};

// Fixed-size GPU textures holding the current cursor bitmap. Allocated once;
// a shape change only uploads the new pixels (UpdateSubresource).
class CursorOverlay {
public:
    static constexpr unsigned kMaxSize = 256;

    explicit CursorOverlay(ID3D11Device* device);

    // Uploads the shape; returns the size that will be drawn (clipped to kMaxSize).
    void upload(ID3D11DeviceContext* ctx, const capture::CursorImage& image);

    unsigned width() const { return m_width; }
    unsigned height() const { return m_height; }
    ID3D11ShaderResourceView* colorSrv() const { return m_colorSrv.Get(); }
    ID3D11ShaderResourceView* invertSrv() const { return m_invertSrv.Get(); }

private:
    ComPtr<ID3D11Texture2D> m_color;
    ComPtr<ID3D11Texture2D> m_invert;
    ComPtr<ID3D11ShaderResourceView> m_colorSrv;
    ComPtr<ID3D11ShaderResourceView> m_invertSrv;
    unsigned m_width = 0;
    unsigned m_height = 0;
};

} // namespace luma::gpu
