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
    std::uint32_t dimTimeoutSeconds{300};
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
    void setDeviceStatus(const DeviceStatus& status);
    void setNotice(const app::SystemNotice& notice, std::uint32_t nowMs);
    void setSettings(const SettingsView& settings);

    [[nodiscard]] Screen screen() const noexcept;
    [[nodiscard]] core::ControlContext controlContext() const noexcept;
    void nextScreen();
    void toggleActions();
    void setBrightness(std::uint8_t brightness);
    void setDimmed(bool dimmed, std::uint8_t normalBrightness);

    void showVolume(std::int16_t percent, bool muted, std::uint32_t nowMs);
    void showTransport(app::PlaybackStatus status, std::uint32_t nowMs);
    void optimisticSeek(std::int32_t offsetMs, std::uint32_t nowMs);
    void showToast(const char* message, std::uint32_t nowMs, bool error = false);
    void showFactoryResetChord(std::uint8_t secondsRemaining);
    void hideFactoryResetChord();
    void showResetting();

  private:
    void render(std::uint32_t nowMs);
    void renderBoot();
    void renderHeader(std::uint32_t nowMs);
    void renderConnection(std::uint32_t nowMs);
    void renderNowPlaying(std::uint32_t nowMs);
    void renderIdle();
    void renderArtwork();
    void renderMetadata(std::uint16_t color, std::int16_t xOffset = 0);
    void renderFooter(std::uint32_t nowMs);
    void renderDevice();
    void renderActions();
    void renderSettings();
    void renderAbout();
    void renderVolumeOverlay(std::uint32_t nowMs);
    void renderToast();
    void renderFactoryResetOverlay();
    void tickTrackTransition(std::uint32_t nowMs);
    void drawFitted(const char* text, std::int32_t x, std::int32_t y, std::int32_t maxWidth,
                    const lgfx::IFont* font, std::uint16_t color,
                    lgfx::textdatum_t datum = lgfx::textdatum_t::top_left);
    void drawTransportIcon(std::int32_t centerX, std::int32_t centerY,
                           app::PlaybackStatus status, std::uint16_t color,
                           std::uint8_t pulse = 0);
    void drawConnectionGlyph(std::int32_t x, std::int32_t y, std::uint32_t nowMs);
    [[nodiscard]] bool connectionScreenActive() const noexcept;
    [[nodiscard]] bool trackChanged(const app::PlaybackSnapshot& snapshot) const noexcept;
    [[nodiscard]] static const char* screenName(Screen screen) noexcept;

    display::DisplayDriver& display_;
    app::PlaybackSnapshot playback_{};
    app::PlaybackSnapshot pendingPlayback_{};
    app::PlayerListSnapshot players_{};
    DeviceStatus status_{};
    SettingsView settings_{};
    core::ProgressClock progress_{};
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
    std::uint32_t lastProgressFrameMs_{0};
    std::int16_t volumeOverlayPercent_{-1};
    std::uint8_t resetSecondsRemaining_{0};
    bool hasPlayback_{false};
    bool hasPendingPlayback_{false};
    bool transitionSwapped_{false};
    bool volumeOverlayMuted_{false};
    bool toastError_{false};
    bool factoryResetChordVisible_{false};
    bool bootRendered_{false};
    bool dimmed_{false};
    bool dirty_{true};
};

}  // namespace deskwave::ui
