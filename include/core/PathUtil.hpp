#pragma once

#include <filesystem>
#include <string>

namespace unnamed::core {

// The core API passes names and virtual paths as UTF-8 std::string. These
// convert to/from std::filesystem::path without going through the (on Windows,
// ANSI) narrow encoding.
inline std::filesystem::path pathFromUtf8(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

} // namespace unnamed::core
