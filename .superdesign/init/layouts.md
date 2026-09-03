# DeskWave Shared Layouts

## Layout model

The app shell, header, and footer are methods of the monolithic `UiController`. Each shared layout method is copied here in full. Now Playing child components are not duplicated; their full implementations are in `components.md`. Display and touch source are included because the physical canvas and coordinate mapping are part of the layout contract.

## Display shell, atmosphere, boot, and header
- Source: `firmware/src/ui/ui_controller.cpp:565`
- Description: Complete top-level compositor, screen switch, overlay priority, fixed atmosphere, boot splash, and persistent 32px header.

```cpp
void UiController::render(const std::uint32_t nowMs) {
    progressPainted_ = false;
    renderAtmosphere();
    renderHeader(nowMs);
    switch (screen_) {
        case Screen::NowPlaying:
            if (connectionScreenActive()) {
                renderConnection(nowMs);
            } else {
                renderNowPlaying(nowMs);
            }
            break;
        case Screen::Device:
            renderDevice();
            break;
        case Screen::Settings:
            renderSettings();
            break;
        case Screen::About:
            renderAbout();
            break;
        case Screen::Actions:
            renderActions();
            break;
    }
    if (factoryResetChordVisible_) {
        renderFactoryResetOverlay();
    } else if (volumeOverlayUntilMs_ != 0) {
        renderVolumeOverlay(nowMs);
    } else if (toastUntilMs_ != 0) {
        renderToast();
    }
}

void UiController::renderAtmosphere() {
    display_.fillScreen(kBackground);
    for (std::int32_t y = 0; y < 240; y += 8) {
        const auto amount = static_cast<std::uint8_t>(28U + (y * 70U / 239U));
        display_.fillRect(0, y, 320, 8, blend565(kBackgroundLift, kBackground, amount));
    }
    display_.fillCircle(301, 42, 72, blend565(kViolet, kBackground, 26));
    display_.fillCircle(18, 222, 68, blend565(kAccent, kBackground, 18));
    display_.fillCircle(265, 236, 48, blend565(kMagenta, kBackground, 15));
    constexpr std::array<std::array<std::int16_t, 2>, 10> stars{{
        {{23, 47}},
        {{51, 29}},
        {{91, 17}},
        {{137, 29}},
        {{212, 18}},
        {{283, 82}},
        {{305, 117}},
        {{21, 141}},
        {{273, 155}},
        {{115, 196}},
    }};
    for (std::size_t index = 0; index < stars.size(); ++index) {
        display_.fillCircle(stars[index][0], stars[index][1], index % 3 == 0 ? 1 : 0,
                            blend565(index % 2 == 0 ? kAccent : kViolet, kBackground, 105));
    }
}

void UiController::renderBoot() {
    renderAtmosphere();
    display_.drawCircle(160, 96, 54, blend565(kViolet, kBackground, 100));
    display_.drawCircle(160, 96, 43, blend565(kAccent, kBackground, 90));
    display_.fillCircle(208, 75, 4, kMagenta);
    for (std::int32_t offset = 0; offset < 3; ++offset) {
        const auto color = offset == 0 ? kAccent : blend565(kViolet, kBackground, 110);
        const auto y = 78 + offset * 9;
        display_.drawBezier(58, y, 104, y - 22, 132, y + 17, 163, y - 4, color);
        display_.drawBezier(163, y - 4, 203, y - 24, 232, y + 18, 265, y - 3, color);
    }
    drawFitted("DeskWave", 160, 125, 280, &fonts::Font4, kText, lgfx::textdatum_t::top_center);
    char version[24];
    std::snprintf(version, sizeof(version), "v%s", deskwave::kVersion);
    drawFitted(version, 160, 163, 120, &fonts::Font2, kTextMuted, lgfx::textdatum_t::top_center);
}

void UiController::renderHeader(const std::uint32_t nowMs) {
    (void)nowMs;
    const auto headerBackground = blend565(kPanel, kBackground, 175);
    display_.fillRect(0, 0, display_.width(), 32, headerBackground);
    display_.fillRect(0, 31, display_.width(), 1, blend565(kViolet, kBackground, 80));

    if (screen_ == Screen::NowPlaying && hasPlayback_) {
        // A small, unmistakable Spotify/player mark replaces the ambiguous
        // top-right affordance. The entire header is informational only.
        display_.fillCircle(15, 15, 9, kSpotify);
        display_.drawBezier(9, 12, 13, 10, 19, 10, 22, 12, kBackground);
        display_.drawBezier(10, 15, 14, 13, 19, 14, 21, 15, kBackground);
        display_.drawBezier(11, 18, 14, 16, 18, 17, 20, 18, kBackground);
        drawFitted("NOW PLAYING", 30, 3, 118, &fonts::Font2, kText);
        drawFitted(playback_.playerName[0] == '\0' ? "Media player" : playback_.playerName, 30,
                   18, 206, &fonts::Font0, kTextMuted);
    } else {
        display_.fillCircle(15, 15, 6, kAccentDim);
        display_.fillCircle(15, 15, 2, kAccent);
        drawFitted("DESKWAVE", 28, 9, 78, &fonts::Font0, kText);
        drawFitted(screenName(screen_), 176, 9, 130, &fonts::Font0, kTextMuted,
                   lgfx::textdatum_t::top_center);
    }

    const auto connectionColor = status_.hostConnected ? kSpotify : kWarning;
    display_.fillCircle(248, 15, 3, connectionColor);
    drawFitted(status_.hostConnected ? "LINKED" : "RETRY", 312, 10, 56, &fonts::Font0,
               connectionColor, lgfx::textdatum_t::top_right);
}

```

## Now Playing layout selector
- Source: `firmware/src/ui/ui_controller.cpp:726`
- Description: Complete active/idle branch and child-component placement order. Full child component methods immediately following this source are in `components.md`.

```cpp
void UiController::renderNowPlaying(const std::uint32_t nowMs) {
    if (!hasPlayback_ || (playback_.playerId[0] == '\0' && playback_.title[0] == '\0') ||
        (playback_.title[0] == '\0' && playback_.status == app::PlaybackStatus::Stopped)) {
        renderIdle();
        return;
    }
    if (titleTextWidth_ == 0) {
        resetTitleScroll(nowMs);
    }
    renderArtwork();
    renderMetadata(nowMs, kText);
    renderFooter(nowMs);
}

void UiController::renderIdle() {
    display_.fillRoundRect(35, 49, 250, 150, 14, kPanel);
    display_.drawRoundRect(35, 49, 250, 150, 14, kLine);
    display_.drawCircle(160, 90, 23, kAccentDim);
    display_.fillCircle(153, 101, 5, kAccent);
    display_.drawLine(158, 100, 158, 77, kAccent);
    display_.drawLine(158, 77, 174, 73, kAccent);
    display_.drawLine(174, 73, 174, 94, kAccent);
    display_.fillCircle(169, 95, 5, kAccent);
    drawFitted("DeskWave", 160, 126, 220, &fonts::Font4, kText, lgfx::textdatum_t::top_center);
    const char* message =
        playback_.playerId[0] == '\0' ? "No active media player" : "No music playing";
    drawFitted(message, 160, 162, 220, &fonts::Font2, kTextMuted, lgfx::textdatum_t::top_center);
    if (playback_.playerName[0] != '\0') {
        drawFitted(playback_.playerName, 160, 184, 210, &fonts::Font0, kAccent,
                   lgfx::textdatum_t::top_center);
    }
}

```

## Progress and transport footer layout
- Source: `firmware/src/ui/ui_controller.cpp:892`
- Description: Complete five-region footer and bounded progress band, aligned to the touch hitboxes.

```cpp
void UiController::renderFooter(const std::uint32_t nowMs) {
    const auto controlsBackground = blend565(kPanel, kBackground, 225);
    const auto playBackground = blend565(kPanelRaised, kBackground, 240);
    progressPainted_ = false;
    display_.fillRect(0, 174, 320, 21, blend565(kPanel, kBackground, 105));
    display_.fillRect(0, 195, 320, 45, controlsBackground);
    display_.fillRect(118, 195, 84, 45, playBackground);
    display_.fillRect(0, 195, 320, 1, blend565(kLine, kBackground, 190));
    constexpr std::array<std::int16_t, 4> dividers{{64, 118, 202, 256}};
    for (const auto divider : dividers) {
        display_.drawFastVLine(divider, 199, 36, blend565(kLine, controlsBackground, 175));
    }
    renderProgress(nowMs);

    const auto shuffleEnabled = playback_.shuffleKnown && playback_.shuffle;
    const auto shuffleColor = shuffleEnabled ? kSpotify : kTextMuted;
    display_.drawLine(20, 204, 25, 204, shuffleColor);
    display_.drawLine(25, 204, 38, 216, shuffleColor);
    display_.drawLine(38, 216, 43, 216, shuffleColor);
    display_.fillTriangle(43, 212, 43, 220, 48, 216, shuffleColor);
    display_.drawLine(20, 216, 25, 216, shuffleColor);
    display_.drawLine(25, 216, 38, 204, shuffleColor);
    display_.drawLine(38, 204, 43, 204, shuffleColor);
    display_.fillTriangle(43, 200, 43, 208, 48, 204, shuffleColor);
    drawFitted("SHUFFLE", 32, 227, 58, &fonts::Font0, shuffleColor,
               lgfx::textdatum_t::top_center);

    display_.drawFastVLine(84, 203, 16, kTextMuted);
    display_.fillTriangle(99, 202, 99, 220, 85, 211, kText);
    drawFitted("PREV", 91, 227, 48, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);

    const std::uint8_t pulse =
        transportPulseUntilMs_ != 0 && static_cast<std::int32_t>(nowMs - transportPulseUntilMs_) < 0
            ? static_cast<std::uint8_t>((transportPulseUntilMs_ - nowMs) / 90U)
            : 0;
    drawTransportIcon(160, 211, playback_.status, kBackground,
                      std::min<std::uint8_t>(pulse, 3));
    drawFitted(playback_.status == app::PlaybackStatus::Playing ? "PAUSE" : "PLAY", 160, 227, 74,
               &fonts::Font0, kText, lgfx::textdatum_t::top_center);

    display_.drawFastVLine(236, 203, 16, kTextMuted);
    display_.fillTriangle(221, 202, 221, 220, 235, 211, kText);
    drawFitted("NEXT", 229, 227, 48, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);

    display_.fillCircle(280, 210, 2, kViolet);
    display_.fillCircle(288, 210, 2, kViolet);
    display_.fillCircle(296, 210, 2, kViolet);
    drawFitted("MORE", 288, 227, 58, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);
}

void UiController::renderProgress(const std::uint32_t nowMs) {
    const auto footerBackground = blend565(kPanel, kBackground, 105);
    char position[16];
    char duration[16];
    formatDuration(progress_.position(nowMs), position);
    formatDuration(progress_.duration(), duration);
    if (!playback_.hasDuration) {
        app::copyText(duration, "--:--");
    }

    const auto progressWidth = static_cast<std::int32_t>(progress_.fraction(nowMs) * 304.0F);
    const bool positionChanged = !progressPainted_ || std::strcmp(renderedPosition_, position) != 0;
    const bool durationChanged = !progressPainted_ || std::strcmp(renderedDuration_, duration) != 0;
    if (!progressPainted_) {
        display_.fillRect(0, 174, 320, 21, footerBackground);
        display_.fillRoundRect(8, 178, 304, 3, 1, kLine);
        if (progressWidth > 0) {
            display_.fillRoundRect(8, 178, progressWidth, 3, 1, kAccent);
            display_.fillCircle(8 + progressWidth, 179, 3, kText);
        }
    } else if (progressWidth != renderedProgressWidth_) {
        const auto start = std::max(8, std::min(renderedProgressWidth_, progressWidth) - 4);
        const auto end = std::max(start + 1,
                                  std::min(312, std::max(renderedProgressWidth_, progressWidth) + 4));
        display_.fillRect(start, 175, end - start, 9, footerBackground);
        display_.fillRoundRect(start, 178, end - start, 3, 1, kLine);
        if (progressWidth > 0) {
            display_.fillRoundRect(8, 178, progressWidth, 3, 1, kAccent);
            display_.fillCircle(8 + progressWidth, 179, 3, kText);
        }
    }
    if (positionChanged) {
        display_.fillRect(0, 184, 70, 11, footerBackground);
        drawFitted(position, 8, 184, 60, &fonts::Font0, kTextMuted);
    }
    if (durationChanged) {
        display_.fillRect(246, 184, 74, 11, footerBackground);
        drawFitted(duration, 312, 184, 60, &fonts::Font0, kTextMuted,
                   lgfx::textdatum_t::top_right);
    }
    renderedProgressWidth_ = progressWidth;
    app::copyText(renderedPosition_, position);
    app::copyText(renderedDuration_, duration);
    progressPainted_ = true;
}

```

## Display boundary declaration
- Source: `firmware/include/display/display_driver.h`
- Description: Complete LovyanGFX device boundary owned by every screen.

```cpp
#pragma once

#include <LittleFS.h>

#include <LovyanGFX.hpp>
#include <cstdint>

namespace deskwave::display {

class DisplayDriver final : public lgfx::LGFX_Device {
   public:
    DisplayDriver();
    [[nodiscard]] bool initialize(std::uint8_t brightness);

   private:
    lgfx::Panel_ILI9341 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM backlight_;
};

}  // namespace deskwave::display
```

## Display boundary implementation
- Source: `firmware/src/display/display_driver.cpp`
- Description: Complete ILI9341 bus, panel, backlight, rotation, RGB565 depth, and no-wrap setup.

```cpp
#include "display/display_driver.h"

#include "config/hardware_config.h"

namespace deskwave::display {

DisplayDriver::DisplayDriver() {
    {
        auto configuration = bus_.config();
        configuration.spi_host = SPI2_HOST;
        configuration.spi_mode = 0;
        configuration.freq_write = hardware::kDisplayWriteHz;
        configuration.freq_read = hardware::kDisplayReadHz;
        configuration.spi_3wire = false;
        configuration.use_lock = true;
        configuration.dma_channel = SPI_DMA_CH_AUTO;
        configuration.pin_sclk = hardware::kDisplaySclk;
        configuration.pin_mosi = hardware::kDisplayMosi;
        configuration.pin_miso = hardware::kDisplayMiso;
        configuration.pin_dc = hardware::kDisplayDc;
        bus_.config(configuration);
        panel_.setBus(&bus_);
    }
    {
        auto configuration = panel_.config();
        configuration.pin_cs = hardware::kDisplayCs;
        configuration.pin_rst = hardware::kDisplayReset;
        configuration.pin_busy = -1;
        configuration.panel_width = hardware::kPanelWidth;
        configuration.panel_height = hardware::kPanelHeight;
        configuration.offset_x = 0;
        configuration.offset_y = 0;
        configuration.offset_rotation = 0;
        configuration.readable = true;
        configuration.invert = false;
        configuration.rgb_order = false;
        configuration.dlen_16bit = false;
        configuration.bus_shared = false;
        panel_.config(configuration);
    }
    {
        auto configuration = backlight_.config();
        configuration.pin_bl = hardware::kBacklight;
        configuration.invert = hardware::kBacklightInverted;
        configuration.freq = 20'000;
        configuration.pwm_channel = 7;
        backlight_.config(configuration);
        panel_.setLight(&backlight_);
    }
    setPanel(&panel_);
}

bool DisplayDriver::initialize(const std::uint8_t brightness) {
    if (!LGFX_Device::begin()) {
        return false;
    }
    setRotation(hardware::kDisplayRotation);
    setColorDepth(16);
    setBrightness(brightness);
    setTextWrap(false);
    return width() == hardware::kDisplayWidth && height() == hardware::kDisplayHeight;
}

}  // namespace deskwave::display
```

## Hardware geometry and panel profile
- Source: `firmware/include/config/hardware_config.h`
- Description: Complete panel, orientation, bus, touch, and pin contract, including the published D0WD-V3 profile and legacy compile fallback.

```cpp
#pragma once

#include <array>
#include <cstdint>

#include "deskwave/core/pin_validation.h"

namespace deskwave::hardware {

// Change this file, not application logic, when adapting a board or wiring loom.
#if defined(DESKWAVE_ESP32_D0WD_V3)
// Locally verified ESP32-D0WD-V3 Revision 3.1 board with the 320x240
// ILI9341/XPT2046 panel. The touch panel replaces the encoder and button loom.
inline constexpr char kBoardName[] = "ESP32 Dev Module / ESP32-D0WD-V3";
#else
// Legacy fallback for callers that compile the hardware adapter without the
// published ESP32-D0WD-V3 PlatformIO environment.
inline constexpr char kBoardName[] = "Unsupported hardware (use esp32-d0wd-v3)";
#endif
inline constexpr char kDisplayDriver[] = "ILI9341";
inline constexpr std::uint16_t kPanelWidth = 240;
inline constexpr std::uint16_t kPanelHeight = 320;
inline constexpr std::uint16_t kDisplayWidth = 320;
inline constexpr std::uint16_t kDisplayHeight = 240;
#if defined(DESKWAVE_ESP32_D0WD_V3)
// The touch transform is calibrated independently; rotation 1 is the required
// 180-degree flip for the rendered screen on the Revision 3.1 panel.
inline constexpr std::uint8_t kDisplayRotation = 1;
#else
inline constexpr std::uint8_t kDisplayRotation = 1;
#endif

#if defined(DESKWAVE_ESP32_D0WD_V3)
inline constexpr int kDisplaySclk = 14;
inline constexpr int kDisplayMosi = 13;
inline constexpr int kDisplayMiso = 12;
inline constexpr int kDisplayCs = 15;
inline constexpr int kDisplayDc = 2;
inline constexpr int kDisplayReset = -1;
inline constexpr int kBacklight = 21;
inline constexpr std::uint32_t kDisplayWriteHz = 26'000'000;
inline constexpr std::uint32_t kDisplayReadHz = 8'000'000;
inline constexpr bool kBacklightInverted = false;

inline constexpr int kEncoderA = -1;
inline constexpr int kEncoderB = -1;
inline constexpr int kEncoderSwitch = -1;
inline constexpr int kLeftButton = -1;
inline constexpr int kRightButton = -1;
inline constexpr int kMenuButton = -1;

inline constexpr int kTouchCs = 33;
inline constexpr int kTouchIrq = 36;
inline constexpr int kTouchDin = 32;
inline constexpr int kTouchMiso = 39;
inline constexpr int kTouchClk = 25;
inline constexpr bool kHasRgbStatusLed = true;
inline constexpr int kStatusLedRed = 4;
inline constexpr int kStatusLedGreen = 16;
inline constexpr int kStatusLedBlue = 17;
inline constexpr int kStatusLed = 17;
inline constexpr int kBuzzer = -1;

inline constexpr std::array kAssignedPins{
    kDisplaySclk, kDisplayMosi, kDisplayMiso, kDisplayCs, kDisplayDc, kBacklight,
    kTouchCs,       kTouchIrq,      kTouchDin,     kTouchMiso, kTouchClk,
    kStatusLedRed,  kStatusLedGreen, kStatusLedBlue,
};
#else
inline constexpr int kDisplaySclk = 12;
inline constexpr int kDisplayMosi = 11;
inline constexpr int kDisplayMiso = 13;
inline constexpr int kDisplayCs = 10;
inline constexpr int kDisplayDc = 9;
inline constexpr int kDisplayReset = 8;
inline constexpr int kBacklight = 14;
inline constexpr std::uint32_t kDisplayWriteHz = 40'000'000;
inline constexpr std::uint32_t kDisplayReadHz = 16'000'000;
inline constexpr bool kBacklightInverted = false;

inline constexpr int kEncoderA = 4;
inline constexpr int kEncoderB = 5;
inline constexpr int kEncoderSwitch = 6;
inline constexpr int kLeftButton = 7;
inline constexpr int kRightButton = 15;
inline constexpr int kMenuButton = 16;

inline constexpr int kTouchCs = -1;
inline constexpr int kTouchIrq = -1;
inline constexpr int kTouchDin = -1;
inline constexpr int kTouchMiso = -1;
inline constexpr int kTouchClk = -1;
inline constexpr bool kHasRgbStatusLed = false;
inline constexpr int kStatusLedRed = -1;
inline constexpr int kStatusLedGreen = -1;
inline constexpr int kStatusLedBlue = 17;
inline constexpr int kStatusLed = 17;
inline constexpr int kBuzzer = -1;

inline constexpr std::array kAssignedPins{
    kDisplaySclk,  kDisplayMosi, kDisplayMiso, kDisplayCs, kDisplayDc,
    kDisplayReset, kBacklight,   kEncoderA,    kEncoderB,  kEncoderSwitch,
    kLeftButton,   kRightButton, kMenuButton,  kStatusLed,
};
#endif

static_assert(core::pinsUnique(kAssignedPins), "DeskWave GPIO assignments must be unique");
#if !defined(DESKWAVE_ESP32_D0WD_V3)
static_assert(core::pinsAvoidUnsafeEsp32S3Defaults(kAssignedPins),
              "Legacy fallback wiring uses an unsafe ESP32 strapping or flash GPIO");
#endif

}  // namespace deskwave::hardware
```

## Input manager declaration
- Source: `firmware/include/controls/input_manager.h`
- Description: Complete touch/physical input adapter declaration and context-aware coordinate mapper contract.

```cpp
#pragma once

#include <Arduino.h>

#include "deskwave/core/input_logic.h"

namespace deskwave::controls {

struct InputEvent {
    core::PhysicalControl control{core::PhysicalControl::EncoderButton};
    core::Gesture gesture{core::Gesture::ShortPress};
};

class InputManager {
   public:
    explicit InputManager(QueueHandle_t eventQueue);
    [[nodiscard]] bool begin();
    void poll(std::uint32_t nowMs, std::uint32_t nowUs);
    [[nodiscard]] bool factoryResetChordActive() const noexcept;
    void setControlContext(core::ControlContext context) noexcept;

   private:
    static void taskEntry(void* context);
    void run();
    void publish(core::PhysicalControl control, core::Gesture gesture);
    void processButton(core::ButtonTracker& tracker, bool pressed, core::PhysicalControl control,
                       std::uint32_t nowMs);
#if defined(DESKWAVE_ESP32_D0WD_V3)
    void initializeTouch();
    [[nodiscard]] bool readTouch(std::int16_t& x, std::int16_t& y);
    [[nodiscard]] std::uint16_t touchReadAdc(std::uint8_t command);
    void pollTouch(std::uint32_t nowMs);
    [[nodiscard]] core::PhysicalControl touchControlAt(std::int16_t x, std::int16_t y) const;
#endif

    QueueHandle_t eventQueue_;
    core::EncoderTracker encoder_;
    core::ButtonTracker encoderButton_;
    core::ButtonTracker leftButton_;
    core::ButtonTracker rightButton_;
    core::ButtonTracker menuButton_;
    TaskHandle_t task_{nullptr};
    std::uint32_t lastQueueWarningMs_{0};
    volatile bool factoryResetChordActive_{false};
#if defined(DESKWAVE_ESP32_D0WD_V3)
    core::PhysicalControl touchControl_{core::PhysicalControl::EncoderButton};
    std::uint32_t lastTouchPollMs_{0};
    std::uint32_t touchStartedAtMs_{0};
    std::uint32_t nextTouchRepeatAtMs_{0};
    std::int16_t touchStartX_{0};
    std::int16_t touchStartY_{0};
    std::int16_t touchLastX_{0};
    std::int16_t touchLastY_{0};
    std::uint8_t touchStableSamples_{0};
    bool touchActive_{false};
    bool touchStable_{false};
    bool touchMoved_{false};
    bool touchLongEmitted_{false};
    volatile core::ControlContext controlContext_{core::ControlContext::Playback};
#endif
};

}  // namespace deskwave::controls
```

## Touch hit-region implementation
- Source: `firmware/src/controls/input_manager.cpp:177`
- Description: Complete coordinate-to-control mapping; this is the interaction counterpart of the visual layout.

```cpp
core::PhysicalControl InputManager::touchControlAt(const std::int16_t x,
                                                   const std::int16_t y) const {
    // Keep the upper-right status target actionable even though the rest of the
    // header is informational. It is the only persistent navigation affordance
    // on the touch-only ESP32-D0WD-V3 profile.
    if (x >= 238 && y < 36) {
        return controlContext_ == core::ControlContext::Playback
                   ? core::PhysicalControl::MoreButton
                   : core::PhysicalControl::MenuButton;
    }
    if (controlContext_ == core::ControlContext::Actions) {
        if (y >= 82 && y <= 179) {
            if (x >= 12 && x <= 153) {
                return core::PhysicalControl::ShuffleButton;
            }
            if (x >= 166 && x <= 307) {
                return core::PhysicalControl::RepeatButton;
            }
        }
        if (y >= 195 && x >= 256) {
            return core::PhysicalControl::MoreButton;
        }
        return core::PhysicalControl::NoControl;
    }
    // Match the five full-height footer targets drawn by the now-playing UI.
    if (controlContext_ == core::ControlContext::Playback && y >= 190) {
        if (x < 64) {
            return core::PhysicalControl::ShuffleButton;
        }
        if (x < 118) {
            return core::PhysicalControl::LeftButton;
        }
        if (x < 202) {
            return core::PhysicalControl::EncoderButton;
        }
        if (x < 256) {
            return core::PhysicalControl::RightButton;
        }
        return core::PhysicalControl::MoreButton;
    }
    if (controlContext_ == core::ControlContext::Device && y >= 136 && y <= 205) {
        return core::PhysicalControl::EncoderButton;
    }
    if (controlContext_ == core::ControlContext::Settings && y >= 34 && y <= 211) {
        return core::PhysicalControl::EncoderButton;
    }
    return core::PhysicalControl::NoControl;
}

```
