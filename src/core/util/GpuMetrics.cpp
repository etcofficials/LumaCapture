#include "util/GpuMetrics.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <format>
#include <map>
#include <string_view>

namespace luma {
namespace {

// Instance format: pid_1234_luid_0x00000000_0x0000C5F2_phys_0_eng_0_engtype_3D
struct Instance {
    unsigned long pid = 0;
    std::wstring engineKey; // "luid_..._phys_N_eng_N"
    std::wstring engineType;
};

bool parseInstance(std::wstring_view name, Instance& out)
{
    if (!name.starts_with(L"pid_"))
        return false;
    const size_t luid = name.find(L"_luid_");
    const size_t type = name.find(L"_engtype_");
    if (luid == std::wstring_view::npos || type == std::wstring_view::npos || type < luid)
        return false;
    out.pid = std::wcstoul(std::wstring(name.substr(4, luid - 4)).c_str(), nullptr, 10);
    out.engineKey.assign(name.substr(luid + 1, type - luid - 1));
    out.engineType.assign(name.substr(type + 9));
    return true;
}

} // namespace

GpuMetrics::GpuMetrics()
{
    m_pid = GetCurrentProcessId();
    PDH_HQUERY query = nullptr;
    PDH_STATUS st = PdhOpenQueryW(nullptr, 0, &query);
    if (st != ERROR_SUCCESS) {
        m_reason = std::format("PdhOpenQuery failed 0x{:08X}", static_cast<unsigned>(st));
        return;
    }
    PDH_HCOUNTER counter = nullptr;
    st = PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter);
    if (st != ERROR_SUCCESS) {
        m_reason = std::format("GPU Engine counters not available (PdhAddEnglishCounter 0x{:08X})", static_cast<unsigned>(st));
        PdhCloseQuery(query);
        return;
    }
    st = PdhCollectQueryData(query); // prime: rate counters need two collections
    if (st != ERROR_SUCCESS) {
        m_reason = std::format("PdhCollectQueryData failed 0x{:08X}", static_cast<unsigned>(st));
        PdhCloseQuery(query);
        return;
    }
    m_query = query;
    m_counter = counter;
}

GpuMetrics::~GpuMetrics()
{
    if (m_query)
        PdhCloseQuery(static_cast<PDH_HQUERY>(m_query));
}

std::optional<GpuSample> GpuMetrics::sample()
{
    if (!m_query)
        return std::nullopt;
    if (PdhCollectQueryData(static_cast<PDH_HQUERY>(m_query)) != ERROR_SUCCESS)
        return std::nullopt;

    DWORD bytes = static_cast<DWORD>(m_buffer.size());
    DWORD count = 0;
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(m_buffer.data());
    PDH_STATUS st = PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(m_counter), PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                                                 &bytes, &count, items);
    if (st == PDH_MORE_DATA) {
        m_buffer.resize(bytes);
        items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(m_buffer.data());
        st = PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(m_counter), PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes,
                                          &count, items);
    }
    if (st != ERROR_SUCCESS)
        return std::nullopt;

    std::map<std::wstring, double> perEngine;   // all processes
    std::map<std::wstring, double> ownEngine;   // this process
    GpuSample s;
    Instance inst;
    for (DWORD i = 0; i < count; ++i) {
        if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
            continue;
        if (!parseInstance(items[i].szName, inst))
            continue;
        const double v = items[i].FmtValue.doubleValue;
        perEngine[inst.engineKey] += v;
        if (inst.pid == m_pid)
            ownEngine[inst.engineKey] += v;
        if (inst.engineType == L"3D")
            s.engine3dPercent += v;
        else if (inst.engineType == L"Copy")
            s.copyEnginePercent += v;
    }
    for (const auto& [key, v] : perEngine)
        s.busiestEnginePercent = std::max(s.busiestEnginePercent, v);
    for (const auto& [key, v] : ownEngine)
        s.thisProcessPercent = std::max(s.thisProcessPercent, v);
    return s;
}

} // namespace luma
