# DeskWave Theme Context

## Part 1 — Compact token summary

### Rendering model

- Stack: Arduino C++ + LovyanGFX; direct immediate-mode drawing, no web framework or CSS.
- Target: fixed `320 × 240` landscape ILI9341, 16-bit RGB565, rotation `1`.
- Display bus: SPI2, `26 MHz` write / `8 MHz` read on the published ESP32-D0WD-V3 profile.
- Fonts: `fonts::Font0`, `fonts::Font2`, `fonts::Font4`; `fonts::AsciiFont24x48` only for pairing codes.
- Text: no wrap; most fields truncate via `drawFitted()`, while the title uses a `144 × 25` clipped strip.

### Current color tokens

| Token | Source RGB | RGB565 | Main use |
| --- | --- | --- | --- |
| `kBackground` | `#070A12` | `0x0042` | Screen base |
| `kBackgroundLift` | `#101926` | `0x10C4` | Vertical atmospheric lift |
| `kPanel` | `#121D29` | `0x10E5` | Cards and footer |
| `kPanelRaised` | `#1C2A39` | `0x1947` | Raised controls and overlays |
| `kText` | `#F1F4F2` | `0xF7BE` | Primary text |
| `kTextMuted` | `#A9B5BE` | `0xADB7` | Secondary text |
| `kAccent` | `#6FDAC2` | `0x6ED8` | Progress, focus, active details |
| `kAccentDim` | `#225B56` | `0x22CA` | Low-intensity accents |
| `kSpotify` | `#1DB954` | `0x1DCA` | Player/link and current transport fill |
| `kViolet` | `#A891DE` | `0xAC9B` | Secondary atmosphere/details |
| `kMagenta` | `#E098B2` | `0xE4D6` | Faint ambient field |
| `kWarning` | `#E8C077` | `0xEE0E` | Retry/caution |
| `kError` | `#F66969` | `0xF34D` | Errors/destructive confirmation |
| `kLine` | `#373E69` | `0x31ED` | Dividers, rails, borders |

RGB565 displayed approximations differ slightly because red/blue have 5 bits and green has 6. The design target adds runtime artwork-derived `primary`, `secondary`, `darkSupport`, and `foregroundSupport` roles; those are not implemented in this source snapshot and must fall back to the tokens above.

### Geometry, spacing, and radius tokens

| Token | Value |
| --- | --- |
| Canvas | `320 × 240` |
| Header | `32 px` high |
| `kArtworkX`, `kArtworkY`, `kArtworkSize` | `8`, `38`, `132` |
| Metadata rect | `x 150`, `y 38`, `162 × 132` |
| Title clip | `x 159`, `y 56`, `144 × 25` |
| Main horizontal gutter | `8–10 px` |
| Artwork-to-metadata gap | `10 px` |
| Progress rail | `x 8`, `y 178`, `304 × 3` |
| Progress band | `y 174–194` |
| Footer | `y 195–239`, `45 px` high |
| Common radii | `7–10 px` |
| Large panel/overlay radii | `13–16 px` |
| Lines/dividers | `1 px` |

### Motion tokens

| Token | Value | Behavior |
| --- | --- | --- |
| `kTrackTransitionMs` | `220 ms` | Existing metadata fade/slide |
| `kOverlayDurationMs` | `1600 ms` | Volume overlay lifetime |
| `kToastDurationMs` | `2200 ms` | Toast lifetime |
| `kProgressFrameMs` | `500 ms` | Bounded progress update |
| `kTitleFrameMs` | `33 ms` | Title marquee frame target |
| `kTitlePixelsPerSecond` | `34 px/s` | Repeating title speed |
| `kTitleRepeatGap` | `16 px` | Gap between repeated title copies |

Artwork-reactive target recommendations live in `.superdesign/design-system.md`: `750 ms` theme transition, `50 ms` bounded accent frames, `55%` idle intensity, `65%` idle saturation, and `40%` idle glow.

### Shadows and breakpoints

- No shadow engine or alpha blur exists. Depth is built with preblended RGB565 fills, offset shapes, one-pixel outlines, and low-intensity atmospheric circles.
- No responsive breakpoints exist. The only supported viewport is the physical `320 × 240` canvas.

## Part 2 — Raw source dumps

There is no separate theme provider, token module, Tailwind configuration, or global stylesheet. Tokens are file-local constants at the top of `firmware/src/ui/ui_controller.cpp`; the complete owning file is reproduced in `components.md`. The exact token-owning section follows verbatim.

### `firmware/src/ui/ui_controller.cpp` — token and blend source

```cpp
namespace deskwave::ui {
namespace {

constexpr std::uint16_t rgb565(const std::uint8_t red, const std::uint8_t green,
                               const std::uint8_t blue) {
    return static_cast<std::uint16_t>(((red & 0xF8U) << 8U) | ((green & 0xFCU) << 3U) |
                                      (blue >> 3U));
}

constexpr std::uint16_t kBackground = rgb565(7, 10, 18);
constexpr std::uint16_t kBackgroundLift = rgb565(16, 25, 38);
constexpr std::uint16_t kPanel = rgb565(18, 29, 41);
constexpr std::uint16_t kPanelRaised = rgb565(28, 42, 57);
constexpr std::uint16_t kText = rgb565(241, 244, 242);
constexpr std::uint16_t kTextMuted = rgb565(169, 181, 190);
constexpr std::uint16_t kAccent = rgb565(111, 218, 194);
constexpr std::uint16_t kAccentDim = rgb565(34, 91, 86);
constexpr std::uint16_t kSpotify = rgb565(29, 185, 84);
constexpr std::uint16_t kViolet = rgb565(168, 145, 222);
constexpr std::uint16_t kMagenta = rgb565(224, 152, 178);
constexpr std::uint16_t kWarning = rgb565(232, 192, 119);
constexpr std::uint16_t kError = rgb565(246, 105, 105);
constexpr std::uint16_t kLine = rgb565(55, 62, 105);

constexpr std::int32_t kArtworkX = 8;
constexpr std::int32_t kArtworkY = 38;
constexpr std::int32_t kArtworkSize = 132;
constexpr std::int32_t kMetadataX = 150;
constexpr std::int32_t kMetadataY = 38;
constexpr std::int32_t kMetadataWidth = 162;
constexpr std::int32_t kMetadataHeight = 132;
constexpr std::int32_t kTitleX = 159;
constexpr std::int32_t kTitleY = 56;
constexpr std::int32_t kTitleWidth = 144;
constexpr std::int32_t kTitleHeight = 25;
constexpr std::uint32_t kTrackTransitionMs = 220;
constexpr std::uint32_t kOverlayDurationMs = 1'600;
constexpr std::uint32_t kToastDurationMs = 2'200;
constexpr std::uint32_t kProgressFrameMs = 500;
constexpr std::uint32_t kTitleFrameMs = 33;
constexpr std::uint32_t kTitlePixelsPerSecond = 34;
constexpr std::int32_t kTitleRepeatGap = 16;

std::uint16_t blend565(const std::uint16_t foreground, const std::uint16_t background,
                       const std::uint8_t amount) {
    const auto foregroundRed = static_cast<std::uint32_t>((foreground >> 11U) & 0x1FU);
    const auto foregroundGreen = static_cast<std::uint32_t>((foreground >> 5U) & 0x3FU);
    const auto foregroundBlue = static_cast<std::uint32_t>(foreground & 0x1FU);
    const auto backgroundRed = static_cast<std::uint32_t>((background >> 11U) & 0x1FU);
    const auto backgroundGreen = static_cast<std::uint32_t>((background >> 5U) & 0x3FU);
    const auto backgroundBlue = static_cast<std::uint32_t>(background & 0x1FU);
    const auto inverse = static_cast<std::uint32_t>(255U - amount);
    const auto red = (foregroundRed * amount + backgroundRed * inverse) / 255U;
    const auto green = (foregroundGreen * amount + backgroundGreen * inverse) / 255U;
    const auto blue = (foregroundBlue * amount + backgroundBlue * inverse) / 255U;
    return static_cast<std::uint16_t>((red << 11U) | (green << 5U) | blue);
}
```

### `platformio.ini`

```ini
[platformio]
default_envs = esp32-d0wd-v3
src_dir = firmware/src
include_dir = firmware/include
test_dir = firmware/test
data_dir = firmware/data
lib_dir = firmware/lib

[env]
platform = espressif32@6.10.0
framework = arduino
monitor_speed = 115200
extra_scripts = pre:scripts/platformio_version.py
build_flags =
    -Wall
    -Wextra
    -Werror=return-type
    -std=gnu++17
build_unflags =
    -std=gnu++11

[env:esp32-d0wd-v3]
; PlatformIO's esp32dev board definition targets the ESP32-D0WD-V3 module.
board = esp32dev
board_build.filesystem = littlefs
upload_speed = 115200
build_flags =
    ${env.build_flags}
    -DDESKWAVE_ESP32_D0WD_V3=1
lib_deps =
    bblanchon/ArduinoJson@7.4.3
    links2004/WebSockets@2.7.3
    lovyan03/LovyanGFX@1.2.27

[env:native]
platform = native@1.2.1
framework =
test_framework = unity
test_build_src = false
build_flags =
    -std=gnu++17
    -Wall
    -Wextra
    -Werror
```
