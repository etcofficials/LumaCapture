#include "audio/AudioDevices.h"

#include "util/ComScope.h"
#include "util/HResult.h"

#include <windows.h>

#include <mmdeviceapi.h>
#include <initguid.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

namespace luma::audio {

using Microsoft::WRL::ComPtr;

namespace {

std::vector<AudioDeviceInfo> enumerate(EDataFlow flow)
{
    ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)),
          "CoCreateInstance(MMDeviceEnumerator)");

    std::wstring defaultId;
    ComPtr<IMMDevice> def;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &def))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(def->GetId(&id))) {
            defaultId = id;
            CoTaskMemFree(id);
        }
    }

    ComPtr<IMMDeviceCollection> devices;
    check(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices), "EnumAudioEndpoints");
    UINT count = 0;
    devices->GetCount(&count);
    std::vector<AudioDeviceInfo> out;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> dev;
        if (FAILED(devices->Item(i, &dev)))
            continue;
        AudioDeviceInfo info;
        LPWSTR id = nullptr;
        if (SUCCEEDED(dev->GetId(&id))) {
            info.id = id;
            CoTaskMemFree(id);
        }
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                info.name = v.pwszVal;
            PropVariantClear(&v);
        }
        info.isDefault = info.id == defaultId;
        out.push_back(std::move(info));
    }
    return out;
}

} // namespace

std::vector<AudioDeviceInfo> enumerateRenderDevices() { return enumerate(eRender); }
std::vector<AudioDeviceInfo> enumerateCaptureDevices() { return enumerate(eCapture); }

} // namespace luma::audio
