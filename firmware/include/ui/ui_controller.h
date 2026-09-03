#pragma once

#include <Arduino.h>

#include <cstdint>

#include "app/messages.h"
#include "deskwave/core/input_logic.h"
#include "deskwave/core/progress.h"
#include "display/display_driver.h"

namespace deskwave::ui {

enum class Screen : std::uint8_t { NowPlaying, Device, Settings, About, Actions };

struct DeviceStatus {
    core::SystemState state{core::SystemState::Boot};
    char stateLabel[65]{};
    char stateDetail[97]{};
    char ssid[33]{};
    char ipAddress[40]{};
    std::int32_t rssi{0};
    std::uint32_t uptimeSeconds{0};
    std::uint32_t freeHeap{0};
    std::uint32_t largestFreeBlock{0};
    bool wifiConnected{false};
    bool hostConnected{false};
};

struct SettingsView {
    std::uint8_t brightness{180};
    std::uint8_t defaultScreen{0};
    std::uint8_t volumeStepPercent{5};
    std::uint8_t selectedItem{0};
    bool factoryResetConfirmation{false};
};

class UiController {
   public:
    explicit UiController(display::DisplayDriver& display);
    [[nodiscard]] bool begin(std::uint8_t brightness, std::uint8_t defaultScreen,
                             std::uint32_t nowMs);
    void tick(std::uint32_t nowMs);

    void setPlayback(const app::PlaybackSnapshot& snapshot, std::uint32_t nowMs);
    void setArtwork(const app::ArtworkResult& result, std::uint32_t nowMs);
    void setPlayers(const app::PlayerListSnapshot& players, std::uint8_t selectedPlayer);
    void setSelectedPlayer(std::uint8_t selectedPlayer);
    void setClock(const app::ClockSync& clock, std::uint32_t nowMs);
    void setDeviceStatus(const DeviceStatus& status);
    void setNotice(const app::SystemNotice& notice, std::uint32_t nowMs);
    void setSettings(const SettingsView& settings);

    [[nodiscard]] Screen screen() const noexcept;
    [[nodiscard]] core::ControlContext controlContext() const noexcept;
    [[nodiscard]] core::Rgb888 lightColor(std::uint32_t nowMs) const noexcept;
    [[nodiscard]] const char* activeArtworkId() const noexcept;
    [[nodiscard]] const char* stagedArtworkId() const noexcept;
    void nextScreen();
    void toggleActions();
    void setBrightness(std::uint8_t brightness);

    void showVolume(std::int16_t percent, bool muted, std::uint32_t nowMs);
    void showTransport(app::PlaybackStatus status, std::uint32_t nowMs);
    void optimisticSeek(std::int32_t offsetMs, std::uint32_t nowMs);
    void showToast(const char* message, std::uint32_t nowMs, bool error = false);
    void showFactoryResetChord(std::uint8_t secondsRemaining);
    void hideFactoryResetChord();
    void showResetting();

   private:
    struct RenderTheme {
        std::uint16_t primary{0};
        std::uint16_t secondary{0};
        std::uint16_t background{0};
        std::uint16_t foreground{0};
        std::uint8_t glowScale{255};
    };

    void render(std::uint32_t nowMs);
    void renderAtmosphere();
    void renderBoot();
    void renderHeader(std::uint32_t nowMs);
    void renderConnection(std::uint32_t nowMs);
    void renderNowPlaying(std::uint32_t nowMs);
    void renderIdle(std::uint32_t nowMs);
    void renderIdleAnimation(std::uint32_t nowMs);
    void renderIdleClock(std::uint32_t nowMs, bool clear);
    void renderIdleSpotify(lgfx::LGFXBase& canvas, std::uint32_t nowMs);
    void renderIdleLinkStatus(lgfx::LGFXBase& canvas, const RenderTheme& theme);
    void drawIdleWave(lgfx::LGFXBase& canvas, std::uint32_t phase, std::int32_t baseY,
                      std::int32_t amplitude, std::uint8_t thickness, std::uint16_t color);
    void renderCompactLinkStatus(const RenderTheme& theme);
    void renderNowPlayingBackdrop(const RenderTheme& theme);
    void renderArtwork(std::uint32_t nowMs);
    void renderArtworkGlow(const RenderTheme& theme);
    void renderThemeAccents(std::uint32_t nowMs);
    void renderThemeLabels(const RenderTheme& theme);
    void renderActionAccents(const RenderTheme& theme, bool clear);
    void renderMetadata(std::uint32_t nowMs, std::uint16_t color, std::int16_t xOffset = 0);
    void renderLyricsCard(std::uint32_t nowMs, std::uint16_t color,
                          std::int16_t xOffset = 0);
    void renderTitle(std::uint32_t nowMs, std::uint16_t color, std::int16_t xOffset = 0);
    void resetTitleScroll(std::uint32_t nowMs);
    void renderFooter(std::uint32_t nowMs);
    void renderControlIcons(std::uint32_t nowMs, const RenderTheme& theme, bool clear);
    void renderProgress(std::uint32_t nowMs);
    void renderProgressBar(std::uint32_t nowMs, const RenderTheme& theme);
    void renderDevice();
    void renderActions(std::uint32_t nowMs);
    void renderSettings();
    void renderAbout();
    void renderVolumeOverlay(std::uint32_t nowMs);
    void renderToast();
    void renderFactoryResetOverlay();
    void tickTrackTransition(std::uint32_t nowMs);
    void applyPlayback(const app::PlaybackSnapshot& snapshot, std::uint32_t nowMs);
    void transitionTheme(const core::ThemePalette& theme, std::uint32_t nowMs);
    void setThemeResting(bool resting, std::uint32_t nowMs);
    [[nodiscard]] core::ThemePalette sampledTheme(std::uint32_t nowMs) const noexcept;
    [[nodiscard]] std::uint8_t sampledRestAmount(std::uint32_t nowMs) const noexcept;
    [[nodiscard]] RenderTheme renderTheme(std::uint32_t nowMs) const noexcept;
    [[nodiscard]] std::uint16_t canvasBackground(const RenderTheme& theme) const noexcept;
    [[nodiscard]] std::uint16_t panelBackground(const RenderTheme& theme) const noexcept;
    [[nodiscard]] std::uint16_t raisedPanelBackground(const RenderTheme& theme) const noexcept;
    [[nodiscard]] bool themeAnimating(std::uint32_t nowMs) const noexcept;
    [[nodiscard]] bool commitArtwork(const app::ArtworkResult& result, std::uint32_t nowMs);
    void commitFallback(const app::PlaybackSnapshot& snapshot, std::uint32_t nowMs);
    [[nodiscard]] static bool artworkMatches(const app::PlaybackSnapshot& snapshot,
                                             const app::ArtworkResult& result) noexcept;
    [[nodiscard]] static bool sameTrack(const app::PlaybackSnapshot& left,
                                        const app::PlaybackSnapshot& right) noexcept;
    [[nodiscard]] static bool idlePlayback(const app::PlaybackSnapshot& snapshot) noexcept;
    void drawFitted(const char* text, std::int32_t x, std::int32_t y, std::int32_t maxWidth,
                    const lgfx::IFont* font, std::uint16_t color,
                    lgfx::textdatum_t datum = lgfx::textdatum_t::top_left);
    void drawTransportIcon(std::int32_t centerX, std::int32_t centerY, app::PlaybackStatus status,
                           std::uint16_t fill, std::uint16_t outline, std::uint16_t glyph,
                           std::uint8_t pulse = 0);
    [[nodiscard]] bool connectionScreenActive() const noexcept;
    [[nodiscard]] bool trackChanged(const app::PlaybackSnapshot& snapshot) const noexcept;
    [[nodiscard]] bool lyricsChanged(const app::PlaybackSnapshot& snapshot) const noexcept;
    [[nodiscard]] std::int8_t activeLyricIndex(std::uint32_t nowMs) const noexcept;
    [[nodiscard]] static const char* screenName(Screen screen) noexcept;

    display::DisplayDriver& display_;
    lgfx::LGFX_Sprite idleBand_;
    app::PlaybackSnapshot playback_{};
    app::PlaybackSnapshot pendingPlayback_{};
    app::PlayerListSnapshot players_{};
    DeviceStatus status_{};
    SettingsView settings_{};
    core::ProgressClock progress_{};
    app::ClockSync clock_{};
    app::SystemNotice latestNotice_{};
    Screen screen_{Screen::NowPlaying};
    Screen screenBeforeActions_{Screen::NowPlaying};
    std::uint8_t selectedPlayer_{0};
    char artworkId_[65]{};
    char artworkPath_[96]{};
    char toast_[97]{};
    std::uint32_t bootUntilMs_{0};
    std::uint32_t transitionStartedAtMs_{0};
    std::uint32_t volumeOverlayUntilMs_{0};
    std::uint32_t transportPulseUntilMs_{0};
    std::uint32_t toastUntilMs_{0};
    std::uint32_t lastAnimationFrameMs_{0};
    std::uint32_t lastIdleFrameMs_{0};
    std::uint32_t lastThemeFrameMs_{0};
    std::uint32_t lastProgressFrameMs_{0};
    std::uint32_t titleScrollStartedAtMs_{0};
    std::uint32_t lastTitleFrameMs_{0};
    std::int16_t volumeOverlayPercent_{-1};
    std::int16_t titleTextWidth_{0};
    std::int8_t renderedLyricIndex_{-2};
    std::int32_t renderedProgressWidth_{0};
    std::uint32_t artworkGeneration_{0};
    std::uint32_t themeTransitionStartedAtMs_{0};
    std::uint32_t restTransitionStartedAtMs_{0};
    std::uint64_t renderedClockMinute_{0};
    char renderedPosition_[16]{};
    char renderedDuration_[16]{};
    std::uint8_t resetSecondsRemaining_{0};
    std::uint8_t restFrom_{255};
    std::uint8_t restTarget_{255};
    core::ThemePalette themeFrom_{};
    core::ThemePalette themeTarget_{};
    app::ArtworkResult stagedArtwork_{};
    bool hasPlayback_{false};
    bool hasPendingPlayback_{false};
    bool hasStagedArtwork_{false};
    bool transitionSwapped_{false};
    bool volumeOverlayMuted_{false};
    bool toastError_{false};
    bool factoryResetChordVisible_{false};
    bool bootRendered_{false};
    bool titleScrollActive_{false};
    bool progressPainted_{false};
    bool clockRendered_{false};
    bool idleBandReady_{false};
    bool dirty_{true};
};

}  // namespace deskwave::ui
