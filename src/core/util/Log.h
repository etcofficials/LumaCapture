#pragma once

#include <filesystem>
#include <format>
#include <string_view>

namespace luma::log {

enum class Level { Debug, Info, Warn, Error };

void setMinLevel(Level level);
// Mirrors every subsequent line into this file (in addition to stdout/stderr).
void openFile(const std::filesystem::path& path, bool append = false);
void closeFile();

void write(Level level, std::string_view message);

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) { write(Level::Debug, std::format(fmt, std::forward<Args>(args)...)); }
template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) { write(Level::Info, std::format(fmt, std::forward<Args>(args)...)); }
template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) { write(Level::Warn, std::format(fmt, std::forward<Args>(args)...)); }
template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) { write(Level::Error, std::format(fmt, std::forward<Args>(args)...)); }

} // namespace luma::log
