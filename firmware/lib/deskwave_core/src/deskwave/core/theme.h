#pragma once

#include <cstdint>

namespace deskwave::core {

struct Rgb888 {
    std::uint8_t red{0};
    std::uint8_t green{0};
    std::uint8_t blue{0};
};

struct ThemePalette {
    Rgb888 primary{};
    Rgb888 secondary{};
    Rgb888 background{};
    Rgb888 foreground{};
};

[[nodiscard]] bool colorsEqual(const Rgb888& left, const Rgb888& right) noexcept;
[[nodiscard]] bool themesEqual(const ThemePalette& left, const ThemePalette& right) noexcept;
[[nodiscard]] Rgb888 unpackRgb(std::uint32_t packed) noexcept;
[[nodiscard]] std::uint32_t packRgb(const Rgb888& color) noexcept;
[[nodiscard]] Rgb888 interpolateColor(const Rgb888& from, const Rgb888& to,
                                      std::uint8_t amount) noexcept;
[[nodiscard]] Rgb888 scaleColor(const Rgb888& color, std::uint8_t scale) noexcept;
[[nodiscard]] Rgb888 rgbLedPwm(const Rgb888& srgb, std::uint8_t intensity,
                               const Rgb888& calibration) noexcept;
[[nodiscard]] ThemePalette interpolateTheme(const ThemePalette& from, const ThemePalette& to,
                                            std::uint8_t amount) noexcept;
[[nodiscard]] Rgb888 softenColor(const Rgb888& color, const Rgb888& background,
                                 std::uint8_t desaturation, std::uint8_t backgroundBlend) noexcept;
[[nodiscard]] std::uint8_t easedProgress(std::uint32_t elapsedMs,
                                         std::uint32_t durationMs) noexcept;

}  // namespace deskwave::core
