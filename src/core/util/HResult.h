#pragma once

#include <windows.h>

#include <source_location>
#include <stdexcept>
#include <string>

namespace luma {

// Human-readable text for an HRESULT, including DXGI/D3D codes.
std::string hresultToString(HRESULT hr);

class HResultError : public std::runtime_error {
public:
    HResultError(HRESULT hr, const std::string& what);
    HRESULT code() const noexcept { return m_hr; }

private:
    HRESULT m_hr;
};

// True for errors that mean the D3D device is gone and must be recreated.
bool isDeviceLost(HRESULT hr) noexcept;

inline void check(HRESULT hr, const char* what, std::source_location loc = std::source_location::current())
{
    if (FAILED(hr))
        throw HResultError(hr, std::string(what) + " (" + loc.file_name() + ":" + std::to_string(loc.line()) + ")");
}

} // namespace luma
