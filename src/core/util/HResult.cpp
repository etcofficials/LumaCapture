#include "util/HResult.h"

#include <dxgi.h>

#include <format>

namespace luma {
namespace {

const char* knownName(HRESULT hr)
{
    switch (hr) {
    case DXGI_ERROR_ACCESS_LOST:          return "DXGI_ERROR_ACCESS_LOST";
    case DXGI_ERROR_WAIT_TIMEOUT:         return "DXGI_ERROR_WAIT_TIMEOUT";
    case DXGI_ERROR_DEVICE_REMOVED:       return "DXGI_ERROR_DEVICE_REMOVED";
    case DXGI_ERROR_DEVICE_RESET:         return "DXGI_ERROR_DEVICE_RESET";
    case DXGI_ERROR_DEVICE_HUNG:          return "DXGI_ERROR_DEVICE_HUNG";
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
    case DXGI_ERROR_UNSUPPORTED:          return "DXGI_ERROR_UNSUPPORTED";
    case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE: return "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE";
    case DXGI_ERROR_SESSION_DISCONNECTED: return "DXGI_ERROR_SESSION_DISCONNECTED";
    case DXGI_ERROR_WAS_STILL_DRAWING:    return "DXGI_ERROR_WAS_STILL_DRAWING";
    case DXGI_ERROR_INVALID_CALL:         return "DXGI_ERROR_INVALID_CALL";
    case E_ACCESSDENIED:                  return "E_ACCESSDENIED";
    case E_OUTOFMEMORY:                   return "E_OUTOFMEMORY";
    case E_INVALIDARG:                    return "E_INVALIDARG";
    default:                              return nullptr;
    }
}

} // namespace

std::string hresultToString(HRESULT hr)
{
    char* buffer = nullptr;
    const DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string text = len ? std::string(buffer, len) : std::string();
    if (buffer)
        LocalFree(buffer);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '.'))
        text.pop_back();

    const char* name = knownName(hr);
    return std::format("0x{:08X}{}{}{}{}", static_cast<unsigned>(hr), name ? " " : "", name ? name : "",
                       text.empty() ? "" : ": ", text);
}

HResultError::HResultError(HRESULT hr, const std::string& what)
    : std::runtime_error(what + " failed: " + hresultToString(hr)), m_hr(hr)
{
}

bool isDeviceLost(HRESULT hr) noexcept
{
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG ||
           hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

} // namespace luma
