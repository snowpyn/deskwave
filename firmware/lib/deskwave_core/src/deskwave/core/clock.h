#pragma once

#include <cstdint>

namespace deskwave::core {

struct LocalDateTime {
    std::int32_t year{1970};
    std::uint8_t month{1};
    std::uint8_t day{1};
    std::uint8_t hour{0};
    std::uint8_t minute{0};
};

struct ClockText {
    char time[6]{"--:--"};
    char period[3]{"--"};
    char date[12]{"---/--/----"};
};

[[nodiscard]] bool localDateTime(std::uint64_t unixMs, std::int32_t utcOffsetSeconds,
                                 LocalDateTime& result) noexcept;
[[nodiscard]] bool formatLocalClock(std::uint64_t unixMs, std::int32_t utcOffsetSeconds,
                                    ClockText& result) noexcept;

}  // namespace deskwave::core
