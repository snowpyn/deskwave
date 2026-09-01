#include "deskwave/core/clock.h"

#include <limits>

namespace deskwave::core {

bool localDateTime(const std::uint64_t unixMs, const std::int32_t utcOffsetSeconds,
                   LocalDateTime& result) noexcept {
    constexpr std::int32_t kMaximumUtcOffsetSeconds = 24 * 60 * 60;
    constexpr std::int64_t kSecondsPerDay = 24 * 60 * 60;
    if (utcOffsetSeconds < -kMaximumUtcOffsetSeconds ||
        utcOffsetSeconds > kMaximumUtcOffsetSeconds ||
        unixMs / 1'000U > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }

    const auto utcSeconds = static_cast<std::int64_t>(unixMs / 1'000U);
    const auto localSeconds = utcSeconds + utcOffsetSeconds;
    if (localSeconds < 0) {
        return false;
    }

    const auto days = localSeconds / kSecondsPerDay;
    const auto secondsOfDay = localSeconds % kSecondsPerDay;

    // Howard Hinnant's civil-from-days conversion, with Unix day zero shifted to
    // the proleptic Gregorian civil epoch. This avoids libc timezone state on-device.
    const auto shiftedDays = days + 719'468;
    const auto era = shiftedDays / 146'097;
    const auto dayOfEra = shiftedDays - era * 146'097;
    const auto yearOfEra =
        (dayOfEra - dayOfEra / 1'460 + dayOfEra / 36'524 - dayOfEra / 146'096) / 365;
    auto year = yearOfEra + era * 400;
    const auto dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const auto monthPrime = (5 * dayOfYear + 2) / 153;
    const auto day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;
    const auto month = monthPrime + (monthPrime < 10 ? 3 : -9);
    year += month <= 2 ? 1 : 0;
    if (year < 1970 || year > 9999) {
        return false;
    }

    result.year = static_cast<std::int32_t>(year);
    result.month = static_cast<std::uint8_t>(month);
    result.day = static_cast<std::uint8_t>(day);
    result.hour = static_cast<std::uint8_t>(secondsOfDay / 3'600);
    result.minute = static_cast<std::uint8_t>((secondsOfDay / 60) % 60);
    return true;
}

bool formatLocalClock(const std::uint64_t unixMs, const std::int32_t utcOffsetSeconds,
                      ClockText& result) noexcept {
    static constexpr const char* kMonths[]{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    LocalDateTime local;
    if (!localDateTime(unixMs, utcOffsetSeconds, local)) {
        result = ClockText{};
        return false;
    }

    const auto hour12 = static_cast<unsigned>(local.hour % 12 == 0 ? 12 : local.hour % 12);
    const auto minute = static_cast<unsigned>(local.minute);
    if (hour12 >= 10) {
        result.time[0] = '1';
        result.time[1] = static_cast<char>('0' + hour12 - 10);
        result.time[2] = ':';
        result.time[3] = static_cast<char>('0' + minute / 10);
        result.time[4] = static_cast<char>('0' + minute % 10);
        result.time[5] = '\0';
    } else {
        result.time[0] = static_cast<char>('0' + hour12);
        result.time[1] = ':';
        result.time[2] = static_cast<char>('0' + minute / 10);
        result.time[3] = static_cast<char>('0' + minute % 10);
        result.time[4] = '\0';
        result.time[5] = '\0';
    }
    result.period[0] = local.hour < 12 ? 'A' : 'P';
    result.period[1] = 'M';
    result.period[2] = '\0';

    const char* month = kMonths[local.month - 1];
    result.date[0] = month[0];
    result.date[1] = month[1];
    result.date[2] = month[2];
    result.date[3] = '/';
    result.date[4] = static_cast<char>('0' + local.day / 10);
    result.date[5] = static_cast<char>('0' + local.day % 10);
    result.date[6] = '/';
    result.date[7] = static_cast<char>('0' + (local.year / 1'000) % 10);
    result.date[8] = static_cast<char>('0' + (local.year / 100) % 10);
    result.date[9] = static_cast<char>('0' + (local.year / 10) % 10);
    result.date[10] = static_cast<char>('0' + local.year % 10);
    result.date[11] = '\0';
    return true;
}

}  // namespace deskwave::core
