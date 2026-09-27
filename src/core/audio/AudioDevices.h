#pragma once

#include <string>
#include <vector>

namespace luma::audio {

struct AudioDeviceInfo {
    std::wstring id;   // IMMDevice id (stable)
    std::wstring name; // friendly name
    bool isDefault = false;
};

// Active playback endpoints (system audio is captured from these via loopback).
std::vector<AudioDeviceInfo> enumerateRenderDevices();
// Active recording endpoints (microphones, line-in, virtual cables).
std::vector<AudioDeviceInfo> enumerateCaptureDevices();

} // namespace luma::audio
