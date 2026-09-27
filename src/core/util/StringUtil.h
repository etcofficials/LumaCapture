#pragma once

#include <string>
#include <string_view>

namespace luma {

std::string toUtf8(std::wstring_view w);
std::wstring fromUtf8(std::string_view s);

} // namespace luma
