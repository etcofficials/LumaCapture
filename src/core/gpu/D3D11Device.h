#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace luma::gpu {

using Microsoft::WRL::ComPtr;

struct OutputInfo {
    unsigned adapterIndex = 0;
    unsigned outputIndex = 0; // index within its adapter
    std::wstring adapterName;
    std::wstring deviceName;  // e.g. \\.\DISPLAY1
    RECT desktopRect{};
    DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_UNSPECIFIED;
    double refreshHz = 0;     // current mode, 0 if it could not be queried
    bool primary = false;
};

// Every output attached to the desktop, across all adapters, in DXGI order.
std::vector<OutputInfo> enumerateOutputs();

// Index into enumerateOutputs() of the monitor containing most of `rect`
// (screen coordinates), or 0.
unsigned outputIndexForRect(const std::vector<OutputInfo>& outputs, const RECT& rect);

// A D3D11 device on the adapter that drives a particular output (Desktop
// Duplication requires the device to live on that adapter), or on the first
// adapter without an output (window capture).
class D3D11Device {
public:
    explicit D3D11Device(const OutputInfo& output);
    // Device for Windows.Graphics.Capture: the frame pool touches the device
    // from its own threads, so multithread protection is enabled.
    struct ForWindowCapture {};
    explicit D3D11Device(ForWindowCapture);

    ID3D11Device* device() const { return m_device.Get(); }
    ID3D11DeviceContext* context() const { return m_context.Get(); }
    IDXGIOutput1* output() const { return m_output.Get(); } // null for window capture devices
    D3D_FEATURE_LEVEL featureLevel() const { return m_featureLevel; }
    const std::wstring& adapterName() const { return m_adapterName; }

    // Reason for device loss, or S_OK while the device is healthy.
    HRESULT removedReason() const { return m_device->GetDeviceRemovedReason(); }

private:
    void create(IDXGIAdapter1* adapter, bool multithreaded);

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIOutput1> m_output;
    D3D_FEATURE_LEVEL m_featureLevel{};
    std::wstring m_adapterName;
};

} // namespace luma::gpu
