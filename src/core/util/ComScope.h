#pragma once

#include <objbase.h>

namespace luma {

// Initialises COM (multithreaded apartment) for the lifetime of the object on
// the current thread. Every worker thread that touches COM/WinRT/MF/WASAPI owns one.
class ComScope {
public:
    ComScope() { m_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope()
    {
        if (SUCCEEDED(m_hr))
            CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    // RPC_E_CHANGED_MODE (thread already STA) still allows COM use.
    bool ok() const { return SUCCEEDED(m_hr) || m_hr == RPC_E_CHANGED_MODE; }

private:
    HRESULT m_hr;
};

} // namespace luma
