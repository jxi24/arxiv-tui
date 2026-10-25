// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>

namespace arxiv_tui::test {

/// Sets TZ for the lifetime of the object and restores it afterwards.
class ScopedTimeZone {
  public:
    explicit ScopedTimeZone(const char* tz) {
        const char* old = std::getenv("TZ");
        m_had_tz = old != nullptr;
        if (old)
            m_saved = old;
        ::setenv("TZ", tz, 1);
        ::tzset();
    }
    ~ScopedTimeZone() {
        if (m_had_tz)
            ::setenv("TZ", m_saved.c_str(), 1);
        else
            ::unsetenv("TZ");
        ::tzset();
    }
    ScopedTimeZone(const ScopedTimeZone&) = delete;
    ScopedTimeZone& operator=(const ScopedTimeZone&) = delete;

  private:
    bool m_had_tz = false;
    std::string m_saved;
};

/// A UTC instant as a time_point.
inline std::chrono::system_clock::time_point utc_time(int year, int month, int day, int hour = 0) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

} // namespace arxiv_tui::test
