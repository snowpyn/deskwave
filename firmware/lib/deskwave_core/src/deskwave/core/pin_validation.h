#pragma once

#include <array>
#include <cstddef>

namespace deskwave::core {

template <std::size_t Size>
constexpr bool pinsUnique(const std::array<int, Size>& pins) noexcept {
    for (std::size_t left = 0; left < Size; ++left) {
        if (pins[left] < 0) {
            continue;
        }
        for (std::size_t right = left + 1; right < Size; ++right) {
            if (pins[left] == pins[right]) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool isUnsafeEsp32S3Pin(const int pin) noexcept {
    return pin == 0 || pin == 3 || (pin >= 26 && pin <= 37) || pin == 45 || pin == 46;
}

template <std::size_t Size>
constexpr bool pinsAvoidUnsafeEsp32S3Defaults(const std::array<int, Size>& pins) noexcept {
    for (const int pin : pins) {
        if (pin >= 0 && isUnsafeEsp32S3Pin(pin)) {
            return false;
        }
    }
    return true;
}

}  // namespace deskwave::core
