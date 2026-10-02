#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace unnamed::core {

// "1.50 MiB (1572864 bytes)"-style size for property values.
inline std::string humanSize(std::uint64_t bytes) {
    static const char* units[] = {"bytes", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    char buf[64];
    if (unit == 0)
        std::snprintf(buf, sizeof buf, "%llu bytes", static_cast<unsigned long long>(bytes));
    else
        std::snprintf(buf, sizeof buf, "%.2f %s (%llu bytes)", value, units[unit],
                      static_cast<unsigned long long>(bytes));
    return buf;
}

// "2026-10-02 14:03 UTC"; empty if unknown.
inline std::string formatTimestamp(std::int64_t secs) {
    if (secs <= 0)
        return {};
    const std::time_t t = static_cast<std::time_t>(secs);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M UTC", &tm);
    return buf;
}

} // namespace unnamed::core
