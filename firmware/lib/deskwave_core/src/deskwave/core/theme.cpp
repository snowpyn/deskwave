#include "deskwave/core/theme.h"

#include <algorithm>

namespace deskwave::core {
namespace {

std::uint8_t interpolateChannel(const std::uint8_t from, const std::uint8_t to,
                                const std::uint8_t amount) noexcept {
    const auto inverse = static_cast<std::uint32_t>(255U - amount);
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(from) * inverse +
                                      static_cast<std::uint32_t>(to) * amount + 127U) /
                                     255U);
}

}  // namespace

bool colorsEqual(const Rgb888& left, const Rgb888& right) noexcept {
    return left.red == right.red && left.green == right.green && left.blue == right.blue;
}

bool themesEqual(const ThemePalette& left, const ThemePalette& right) noexcept {
    return colorsEqual(left.primary, right.primary) &&
           colorsEqual(left.secondary, right.secondary) &&
           colorsEqual(left.background, right.background) &&
           colorsEqual(left.foreground, right.foreground);
}

Rgb888 unpackRgb(const std::uint32_t packed) noexcept {
    return {
        static_cast<std::uint8_t>((packed >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((packed >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(packed & 0xFFU),
    };
}

std::uint32_t packRgb(const Rgb888& color) noexcept {
    return (static_cast<std::uint32_t>(color.red) << 16U) |
           (static_cast<std::uint32_t>(color.green) << 8U) | static_cast<std::uint32_t>(color.blue);
}

Rgb888 interpolateColor(const Rgb888& from, const Rgb888& to, const std::uint8_t amount) noexcept {
    return {
        interpolateChannel(from.red, to.red, amount),
        interpolateChannel(from.green, to.green, amount),
        interpolateChannel(from.blue, to.blue, amount),
    };
}

Rgb888 scaleColor(const Rgb888& color, const std::uint8_t scale) noexcept {
    const auto scaled = [scale](const std::uint8_t channel) {
        return static_cast<std::uint8_t>((static_cast<std::uint16_t>(channel) * scale + 127U) /
                                         255U);
    };
    return {scaled(color.red), scaled(color.green), scaled(color.blue)};
}

Rgb888 normalizeColor(const Rgb888& color) noexcept {
    const auto peak = std::max({color.red, color.green, color.blue});
    if (peak == 0) {
        return {};
    }
    const auto normalized = [peak](const std::uint8_t channel) {
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(channel) * 255U + peak / 2U) /
                                         peak);
    };
    return {normalized(color.red), normalized(color.green), normalized(color.blue)};
}

Rgb888 rgbLedPwm(const Rgb888& srgb, const std::uint8_t intensity,
                 const Rgb888& calibration) noexcept {
    // TFT theme values are sRGB, but LED PWM duty controls approximately linear
    // light output. A compact gamma-2 conversion keeps the physical LED's
    // perceived hue aligned with the display instead of washing out mid-tones.
    const auto linearize = [](const std::uint8_t channel) {
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(channel) * channel + 127U) /
                                         255U);
    };
    const Rgb888 linear{linearize(srgb.red), linearize(srgb.green), linearize(srgb.blue)};
    const Rgb888 balanced{
        static_cast<std::uint8_t>(
            (static_cast<std::uint16_t>(linear.red) * calibration.red + 127U) / 255U),
        static_cast<std::uint8_t>(
            (static_cast<std::uint16_t>(linear.green) * calibration.green + 127U) / 255U),
        static_cast<std::uint8_t>(
            (static_cast<std::uint16_t>(linear.blue) * calibration.blue + 127U) / 255U),
    };
    return scaleColor(balanced, intensity);
}

ThemePalette interpolateTheme(const ThemePalette& from, const ThemePalette& to,
                              const std::uint8_t amount) noexcept {
    return {
        interpolateColor(from.primary, to.primary, amount),
        interpolateColor(from.secondary, to.secondary, amount),
        interpolateColor(from.background, to.background, amount),
        interpolateColor(from.foreground, to.foreground, amount),
    };
}

Rgb888 softenColor(const Rgb888& color, const Rgb888& background, const std::uint8_t desaturation,
                   const std::uint8_t backgroundBlend) noexcept {
    const auto luminance =
        static_cast<std::uint8_t>((static_cast<std::uint32_t>(color.red) * 54U +
                                   static_cast<std::uint32_t>(color.green) * 183U +
                                   static_cast<std::uint32_t>(color.blue) * 19U + 128U) /
                                  256U);
    const Rgb888 neutral{luminance, luminance, luminance};
    return interpolateColor(interpolateColor(color, neutral, desaturation), background,
                            backgroundBlend);
}

std::uint8_t easedProgress(const std::uint32_t elapsedMs, const std::uint32_t durationMs) noexcept {
    if (durationMs == 0 || elapsedMs >= durationMs) {
        return 255;
    }
    const auto linear = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(255U, (static_cast<std::uint64_t>(elapsedMs) * 255U) / durationMs));
    const auto squared = static_cast<std::uint64_t>(linear) * linear;
    const auto eased = squared * (765U - 2U * linear);
    return static_cast<std::uint8_t>(std::min<std::uint64_t>(255U, (eased + 32'512U) / 65'025U));
}

}  // namespace deskwave::core
