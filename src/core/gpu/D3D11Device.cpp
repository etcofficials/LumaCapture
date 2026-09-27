#include "gpu/D3D11Device.h"

#include "util/HResult.h"

#include <windows.h>

#include <algorithm>

namespace luma::gpu {
namespace {

double queryRefreshHz(const wchar_t* deviceName)
{
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(deviceName, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
        return static_cast<double>(mode.dmDisplayFrequency);
    return 0;
}

} // namespace

std::vector<OutputInfo> enumerateOutputs()
{
    ComPtr<IDXGIFactory1> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");

    std::vector<OutputInfo> result;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++a) {
        DXGI_ADAPTER_DESC1 ad{};
        adapter->GetDesc1(&ad);
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC od{};
            output->GetDesc(&od);
            if (!od.AttachedToDesktop)
                continue;
            OutputInfo info;
            info.adapterIndex = a;
            info.outputIndex = o;
            info.adapterName = ad.Description;
            info.deviceName = od.DeviceName;
            info.desktopRect = od.DesktopCoordinates;
            info.rotation = od.Rotation;
            info.refreshHz = queryRefreshHz(od.DeviceName);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(od.Monitor, &mi))
                info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            result.push_back(std::move(info));
        }
    }
    return result;
}

unsigned outputIndexForRect(const std::vector<OutputInfo>& outputs, const RECT& rect)
{
    unsigned best = 0;
    long bestArea = -1;
    for (unsigned i = 0; i < outputs.size(); ++i) {
        const RECT& d = outputs[i].desktopRect;
        const long w = std::min(rect.right, d.right) - std::max(rect.left, d.left);
        const long h = std::min(rect.bottom, d.bottom) - std::max(rect.top, d.top);
        const long area = (w > 0 && h > 0) ? w * h : 0;
        if (area > bestArea) {
            bestArea = area;
            best = i;
        }
    }
    return best;
}

D3D11Device::D3D11Device(const OutputInfo& info)
{
    ComPtr<IDXGIFactory1> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    ComPtr<IDXGIAdapter1> adapter;
    check(factory->EnumAdapters1(info.adapterIndex, &adapter), "EnumAdapters1");
    ComPtr<IDXGIOutput> output;
    check(adapter->EnumOutputs(info.outputIndex, &output), "EnumOutputs");
    check(output.As(&m_output), "QueryInterface(IDXGIOutput1)");
    create(adapter.Get(), false);
}

D3D11Device::D3D11Device(ForWindowCapture)
{
    ComPtr<IDXGIFactory1> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    ComPtr<IDXGIAdapter1> adapter;
    check(factory->EnumAdapters1(0, &adapter), "EnumAdapters1");
    create(adapter.Get(), true);
}

void D3D11Device::create(IDXGIAdapter1* adapter, bool multithreaded)
{
    DXGI_ADAPTER_DESC1 ad{};
    adapter->GetDesc1(&ad);
    m_adapterName = ad.Description;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (!multithreaded)
        flags |= D3D11_CREATE_DEVICE_SINGLETHREADED;
    // Driver type must be UNKNOWN when an explicit adapter is passed.
    check(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels,
                            static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &m_device, &m_featureLevel,
                            &m_context),
          "D3D11CreateDevice");
    if (multithreaded) {
        ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(m_context.As(&mt)))
            mt->SetMultithreadProtected(TRUE);
    }
}

} // namespace luma::gpu
