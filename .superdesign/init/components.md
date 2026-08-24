# DeskWave Shared UI Components

## Framework and component model

DeskWave is a custom immediate-mode embedded frontend: Arduino C++ and LovyanGFX draw directly to a fixed RGB565 display. There is no browser framework, component library, CSS layer, or independent component directory. Reusable primitives and Now Playing subcomponents are coherent methods of one `UiController` class.

The declaration and normalized view models are included in full. Implementation sections include each complete method relevant to reusable Now Playing state, artwork, metadata, transport, and text rendering. Page-specific Device/Settings/About bodies are routed in `pages.md`; shared shell geometry is in `layouts.md`.

## UiController declaration
- File: `firmware/include/ui/ui_controller.h`
- Component: `UiController`
- Description: Complete public inputs, renderer methods, state, animation clocks, cached artwork identity, and dirty-region bookkeeping.

```cpp
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
    void renderAtmosphere();
    void renderBoot();
    void renderHeader(std::uint32_t nowMs);
    void renderConnection(std::uint32_t nowMs);
    void renderNowPlaying(std::uint32_t nowMs);
    void renderIdle();
    void renderArtwork();
    void renderMetadata(std::uint32_t nowMs, std::uint16_t color, std::int16_t xOffset = 0);
    void renderTitle(std::uint32_t nowMs, std::uint16_t color, std::int16_t xOffset = 0);
    void resetTitleScroll(std::uint32_t nowMs);
    void renderFooter(std::uint32_t nowMs);
    void renderProgress(std::uint32_t nowMs);
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
    void drawTransportIcon(std::int32_t centerX, std::int32_t centerY, app::PlaybackStatus status,
                           std::uint16_t color, std::uint8_t pulse = 0);
    [[nodiscard]] bool connectionScreenActive() const noexcept;
    [[nodiscard]] bool trackChanged(const app::PlaybackSnapshot& snapshot) const noexcept;
    [[nodiscard]] bool queueChanged(const app::PlaybackSnapshot& snapshot) const noexcept;
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
    std::uint32_t titleScrollStartedAtMs_{0};
    std::uint32_t lastTitleFrameMs_{0};
    std::int16_t volumeOverlayPercent_{-1};
    std::int16_t titleTextWidth_{0};
    std::int32_t renderedProgressWidth_{0};
    char renderedPosition_[16]{};
    char renderedDuration_[16]{};
    std::uint8_t resetSecondsRemaining_{0};
    bool hasPlayback_{false};
    bool hasPendingPlayback_{false};
    bool transitionSwapped_{false};
    bool volumeOverlayMuted_{false};
    bool toastError_{false};
    bool factoryResetChordVisible_{false};
    bool bootRendered_{false};
    bool dimmed_{false};
    bool titleScrollActive_{false};
    bool progressPainted_{false};
    bool dirty_{true};
};

}  // namespace deskwave::ui
```

## UI view-model contract
- File: `firmware/include/app/messages.h`
- Component: playback/artwork/notice/player fixed-size models
- Description: Complete normalized data contract consumed by the renderer.

```cpp
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "deskwave/core/application_state.h"

namespace deskwave::app {

template <std::size_t Size>
void copyText(char (&destination)[Size], const char* source) noexcept {
    static_assert(Size > 0);
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }
    const auto length = std::min(Size - 1, std::strlen(source));
    std::memcpy(destination, source, length);
    destination[length] = '\0';
}

enum class PlaybackStatus : std::uint8_t { Stopped, Playing, Paused };
enum class RepeatMode : std::uint8_t { Unknown, Off, Track, Playlist };

inline constexpr std::size_t kMaximumQueueItems = 4;

struct QueueEntry {
    char title[129]{};
    char artist[129]{};
    char trackId[129]{};
};

struct PlaybackSnapshot {
    char title[129]{};
    char artist[129]{};
    char album[129]{};
    char playerName[65]{};
    char playerId[129]{};
    char trackId[129]{};
    char artworkId[65]{};
    char artworkPath[129]{};
    std::uint64_t durationMs{0};
    std::uint64_t positionMs{0};
    std::uint32_t receivedAtMs{0};
    std::int16_t volumePercent{-1};
    PlaybackStatus status{PlaybackStatus::Stopped};
    RepeatMode repeat{RepeatMode::Unknown};
    bool hasDuration{false};
    bool mutedKnown{false};
    bool muted{false};
    bool shuffleKnown{false};
    bool shuffle{false};
    bool canSeek{false};
    bool canNext{false};
    bool canPrevious{false};
    bool canControl{false};
    std::array<QueueEntry, kMaximumQueueItems> queue{};
    std::uint8_t queueCount{0};
    bool queueAvailable{false};
};

enum class SystemNoticeType : std::uint8_t {
    StateChanged,
    ProvisioningStarted,
    PairingCode,
    NetworkDetails,
    RecoverableError,
    CommandFailed,
};

struct SystemNotice {
    SystemNoticeType type{SystemNoticeType::StateChanged};
    core::SystemState state{core::SystemState::Boot};
    char primary[65]{};
    char secondary[97]{};
    std::int32_t value{0};
};

enum class HostCommand : std::uint8_t {
    Play,
    Pause,
    Toggle,
    Previous,
    Next,
    VolumeUp,
    VolumeDown,
    SetVolume,
    Mute,
    Seek,
    ShuffleToggle,
    SetRepeat,
    Refresh,
    SelectPlayer,
    ListPlayers,
};

struct ControlRequest {
    HostCommand command{HostCommand::Refresh};
    std::int32_t integerValue{0};
    float decimalValue{0.0F};
    char textValue[129]{};
};

struct CommandFeedback {
    std::uint32_t requestSequence{0};
    bool success{false};
    char command[24]{};
    char error[97]{};
};

struct ArtworkRequest {
    char host[64]{};
    std::uint16_t port{0};
    char path[129]{};
    char artworkId[65]{};
    char token[129]{};
};

struct ArtworkResult {
    bool success{false};
    char artworkId[65]{};
    char localPath[96]{};
    char error[80]{};
};

inline constexpr std::size_t kMaximumPlayers = 6;

struct PlayerSummary {
    char id[129]{};
    char name[65]{};
    PlaybackStatus status{PlaybackStatus::Stopped};
};

struct PlayerListSnapshot {
    std::array<PlayerSummary, kMaximumPlayers> players{};
    std::uint32_t receivedAtMs{0};
    std::uint8_t count{0};
};

}  // namespace deskwave::app
```

## Playback and artwork identity state
- File: `firmware/src/ui/ui_controller.cpp:217`
- Components: track/queue comparison, snapshot update, artwork result acceptance
- Description: Complete current logic deciding when transitions start and whether downloaded artwork belongs to visible or pending playback.

```cpp
bool UiController::trackChanged(const app::PlaybackSnapshot& snapshot) const noexcept {
    if (!hasPlayback_) {
        return false;
    }
    if (playback_.trackId[0] != '\0' && snapshot.trackId[0] != '\0') {
        return std::strcmp(playback_.trackId, snapshot.trackId) != 0;
    }
    return std::strcmp(playback_.title, snapshot.title) != 0 ||
           std::strcmp(playback_.artist, snapshot.artist) != 0 ||
           std::strcmp(playback_.album, snapshot.album) != 0;
}

bool UiController::queueChanged(const app::PlaybackSnapshot& snapshot) const noexcept {
    if (!hasPlayback_ || playback_.queueAvailable != snapshot.queueAvailable ||
        playback_.queueCount != snapshot.queueCount) {
        return true;
    }
    for (std::uint8_t index = 0; index < snapshot.queueCount; ++index) {
        const auto& current = playback_.queue[index];
        const auto& next = snapshot.queue[index];
        if (std::strcmp(current.title, next.title) != 0 ||
            std::strcmp(current.artist, next.artist) != 0 ||
            std::strcmp(current.trackId, next.trackId) != 0) {
            return true;
        }
    }
    return false;
}

void UiController::setPlayback(const app::PlaybackSnapshot& snapshot, const std::uint32_t nowMs) {
    if (trackChanged(snapshot) && screen_ == Screen::NowPlaying && !connectionScreenActive()) {
        pendingPlayback_ = snapshot;
        hasPendingPlayback_ = true;
        transitionSwapped_ = false;
        transitionStartedAtMs_ = nowMs;
        return;
    }
    const bool titleChanged = !hasPlayback_ || std::strcmp(playback_.title, snapshot.title) != 0;
    const bool headerChanged = !hasPlayback_ ||
                               std::strcmp(playback_.playerName, snapshot.playerName) != 0;
    const bool metadataChanged = !hasPlayback_ ||
                                 std::strcmp(playback_.title, snapshot.title) != 0 ||
                                 std::strcmp(playback_.artist, snapshot.artist) != 0 ||
                                 queueChanged(snapshot);
    const bool footerChanged = !hasPlayback_ || playback_.status != snapshot.status ||
                               playback_.shuffleKnown != snapshot.shuffleKnown ||
                               playback_.shuffle != snapshot.shuffle ||
                               playback_.hasDuration != snapshot.hasDuration ||
                               playback_.durationMs != snapshot.durationMs;
    const bool actionsChanged = !hasPlayback_ || playback_.shuffleKnown != snapshot.shuffleKnown ||
                                playback_.shuffle != snapshot.shuffle ||
                                playback_.repeat != snapshot.repeat;
    playback_ = snapshot;
    hasPlayback_ = true;
    if (titleChanged) {
        resetTitleScroll(nowMs);
    }
    progress_.synchronize(snapshot.positionMs, snapshot.hasDuration ? snapshot.durationMs : 0,
                          snapshot.status == app::PlaybackStatus::Playing, nowMs);
    if (screen_ == Screen::NowPlaying && !connectionScreenActive() && volumeOverlayUntilMs_ == 0 &&
        toastUntilMs_ == 0 && !bootRendered_) {
        if (headerChanged) {
            renderHeader(nowMs);
        }
        if (metadataChanged) {
            renderMetadata(nowMs, kText);
        }
        if (footerChanged) {
            renderFooter(nowMs);
        } else {
            renderProgress(nowMs);
        }
    } else if (bootRendered_ || (screen_ == Screen::Actions && actionsChanged)) {
        dirty_ = true;
    }
}

void UiController::setArtwork(const app::ArtworkResult& result, const std::uint32_t nowMs) {
    (void)nowMs;
    const bool belongsToCurrentPlayback =
        hasPlayback_ && result.artworkId[0] != '\0' &&
        std::strcmp(playback_.artworkId, result.artworkId) == 0;
    const bool belongsToPendingPlayback =
        hasPendingPlayback_ && result.artworkId[0] != '\0' &&
        std::strcmp(pendingPlayback_.artworkId, result.artworkId) == 0;
    if (!belongsToCurrentPlayback && !belongsToPendingPlayback) {
        // A download can finish after the user skips again. Do not let that
        // late result replace the cover associated with the visible track.
        return;
    }
    if (!result.success) {
        if (result.error[0] != '\0') {
            showToast(result.error, millis(), true);
        }
        return;
    }
    if (std::strcmp(artworkId_, result.artworkId) == 0 &&
        std::strcmp(artworkPath_, result.localPath) == 0) {
        return;
    }
    app::copyText(artworkId_, result.artworkId);
    app::copyText(artworkPath_, result.localPath);
    if (screen_ == Screen::NowPlaying && !connectionScreenActive() &&
        std::strcmp(playback_.artworkId, artworkId_) == 0 && !hasPendingPlayback_) {
        renderArtwork();
    }
}

void UiController::setPlayers(const app::PlayerListSnapshot& players,
                              const std::uint8_t selectedPlayer) {
```

## Track transition controller
- File: `firmware/src/ui/ui_controller.cpp:517`
- Component: `tickTrackTransition`
- Description: Complete current 220ms metadata fade/slide and mid-transition snapshot/artwork/footer swap.

```cpp
void UiController::tickTrackTransition(const std::uint32_t nowMs) {
    if (screen_ != Screen::NowPlaying || connectionScreenActive()) {
        playback_ = pendingPlayback_;
        hasPlayback_ = true;
        hasPendingPlayback_ = false;
        resetTitleScroll(nowMs);
        progress_.synchronize(playback_.positionMs,
                              playback_.hasDuration ? playback_.durationMs : 0,
                              playback_.status == app::PlaybackStatus::Playing, nowMs);
        dirty_ = true;
        return;
    }
    if (volumeOverlayUntilMs_ != 0 || toastUntilMs_ != 0 ||
        static_cast<std::uint32_t>(nowMs - lastAnimationFrameMs_) < 28) {
        return;
    }
    const auto elapsed = static_cast<std::uint32_t>(nowMs - transitionStartedAtMs_);
    const auto half = kTrackTransitionMs / 2;
    if (elapsed < half) {
        const auto amount = static_cast<std::uint8_t>(255U - (elapsed * 255U / half));
        renderMetadata(nowMs, blend565(kText, kBackground, amount),
                       -static_cast<std::int16_t>(elapsed * 6U / half));
    } else {
        if (!transitionSwapped_) {
            playback_ = pendingPlayback_;
            hasPlayback_ = true;
            transitionSwapped_ = true;
            resetTitleScroll(nowMs);
            progress_.synchronize(playback_.positionMs,
                                  playback_.hasDuration ? playback_.durationMs : 0,
                                  playback_.status == app::PlaybackStatus::Playing, nowMs);
            renderHeader(nowMs);
            renderArtwork();
            renderFooter(nowMs);
        }
        const auto secondElapsed = std::min<std::uint32_t>(elapsed - half, half);
        const auto amount = static_cast<std::uint8_t>(secondElapsed * 255U / half);
        renderMetadata(nowMs, blend565(kText, kBackground, amount),
                       static_cast<std::int16_t>(6 - secondElapsed * 6U / half));
    }
    lastAnimationFrameMs_ = nowMs;
    if (elapsed >= kTrackTransitionMs) {
        hasPendingPlayback_ = false;
        transitionSwapped_ = false;
        renderMetadata(nowMs, kText);
    }
}
```

## Now Playing component group
- File: `firmware/src/ui/ui_controller.cpp:726`
- Components: active/idle selector, IdleCard, ArtworkTile, ScrollingTitleStrip, MetadataCard, QueuePreview, TransportIcon, TransportFooter, ProgressBand
- Description: Complete implementation of every current Now Playing visual below the shared header.

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

void UiController::renderArtwork() {
    display_.fillRoundRect(kArtworkX + 4, kArtworkY + 4, kArtworkSize, kArtworkSize, 10,
                           blend565(kViolet, kBackground, 55));
    display_.drawRoundRect(kArtworkX - 1, kArtworkY - 1, kArtworkSize + 2, kArtworkSize + 2, 9,
                           blend565(kAccent, kBackground, 105));
    display_.fillRoundRect(kArtworkX, kArtworkY, kArtworkSize, kArtworkSize, 8, kPanelRaised);
    bool rendered = false;
    if (playback_.artworkId[0] != '\0' && std::strcmp(playback_.artworkId, artworkId_) == 0 &&
        artworkPath_[0] != '\0' && LittleFS.exists(artworkPath_)) {
        rendered =
            display_.drawJpgFile(LittleFS, artworkPath_, kArtworkX, kArtworkY, kArtworkSize,
                                 kArtworkSize, 0, 0, -1.0F, -1.0F, lgfx::textdatum_t::top_left);
    }
    if (!rendered) {
        display_.fillRoundRect(kArtworkX, kArtworkY, kArtworkSize, kArtworkSize, 8, kPanelRaised);
        for (std::int32_t row = 0; row < 4; ++row) {
            const auto y = kArtworkY + 39 + row * 17;
            const auto color = blend565(row % 2 == 0 ? kAccent : kViolet, kPanelRaised,
                                        static_cast<std::uint8_t>(185 - row * 25));
            display_.drawBezier(kArtworkX + 19, y, kArtworkX + 46, y - 15, kArtworkX + 72, y + 14,
                                kArtworkX + 94, y - 2, color);
            display_.drawBezier(kArtworkX + 94, y - 2, kArtworkX + 109, y - 11, kArtworkX + 118,
                                y + 8, kArtworkX + 126, y, color);
        }
        display_.fillCircle(kArtworkX + 69, kArtworkY + 77, 6, kText);
        display_.drawLine(kArtworkX + 75, kArtworkY + 76, kArtworkX + 75, kArtworkY + 48, kText);
        display_.drawLine(kArtworkX + 75, kArtworkY + 48, kArtworkX + 94, kArtworkY + 43, kText);
    }
    display_.drawRoundRect(kArtworkX, kArtworkY, kArtworkSize, kArtworkSize, 8,
                           blend565(kText, kLine, 70));
}

void UiController::resetTitleScroll(const std::uint32_t nowMs) {
    const char* title = playback_.title[0] == '\0' ? "Untitled" : playback_.title;
    display_.setFont(&fonts::Font4);
    titleTextWidth_ = static_cast<std::int16_t>(display_.textWidth(title));
    titleScrollActive_ = titleTextWidth_ > kTitleWidth;
    titleScrollStartedAtMs_ = nowMs;
    lastTitleFrameMs_ = 0;
}

void UiController::renderTitle(const std::uint32_t nowMs, const std::uint16_t color,
                               const std::int16_t xOffset) {
    const char* title = playback_.title[0] == '\0' ? "Untitled" : playback_.title;
    if (titleTextWidth_ <= 0) {
        resetTitleScroll(nowMs);
    }

    std::int32_t scrollOffset = 0;
    if (titleScrollActive_ && xOffset == 0) {
        const auto cycle = static_cast<std::uint32_t>(
            std::max<std::int16_t>(titleTextWidth_, 1) + kTitleRepeatGap);
        const auto phase = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(nowMs - titleScrollStartedAtMs_) *
             kTitlePixelsPerSecond / 1'000U) % cycle);
        scrollOffset = -static_cast<std::int32_t>(phase);
    }

    const auto metadataBackground = blend565(kPanel, kBackground, 235);
    display_.setClipRect(kTitleX, kTitleY, kTitleWidth, kTitleHeight);
    display_.fillRect(kTitleX, kTitleY, kTitleWidth, kTitleHeight, metadataBackground);
    display_.setFont(&fonts::Font4);
    display_.setTextColor(color);
    display_.setTextDatum(lgfx::textdatum_t::top_left);
    const auto titleX = kTitleX + xOffset + scrollOffset;
    display_.drawString(title, titleX, kTitleY + 1);
    if (titleScrollActive_ && xOffset == 0) {
        display_.drawString(title, titleX + titleTextWidth_ + kTitleRepeatGap, kTitleY + 1);
    }
    display_.drawFastHLine(kTitleX, kTitleY + kTitleHeight - 1, kTitleWidth,
                          blend565(kLine, metadataBackground, 150));
    display_.clearClipRect();
}

void UiController::renderMetadata(const std::uint32_t nowMs, const std::uint16_t color,
                                  const std::int16_t xOffset) {
    display_.fillRect(kMetadataX - 3, kMetadataY - 3, kMetadataWidth + 8,
                      kMetadataHeight + 7, kBackground);
    const auto metadataBackground = blend565(kPanel, kBackground, 235);
    display_.fillRoundRect(kMetadataX, kMetadataY, kMetadataWidth, kMetadataHeight, 8,
                           metadataBackground);
    display_.drawRoundRect(kMetadataX, kMetadataY, kMetadataWidth, kMetadataHeight, 8,
                           blend565(kLine, kBackground, 190));

    const auto x = kTitleX + xOffset;
    const auto fadedAccent = color == kText ? kAccent : blend565(kAccent, kBackground, 105);
    const auto fadedArtist = color == kText ? kTextMuted : blend565(kTextMuted, kBackground, 105);
    drawFitted("TRACK", x, 45, 70, &fonts::Font0, fadedAccent);
    renderTitle(nowMs, color, xOffset);
    drawFitted(playback_.artist[0] == '\0' ? "Unknown artist" : playback_.artist, x, 89, 144,
               &fonts::Font2, fadedArtist);

    display_.fillRoundRect(157, 117, 148, 40, 7, blend565(kPanelRaised, metadataBackground, 155));
    display_.drawRoundRect(157, 117, 148, 40, 7,
                           blend565(kLine, metadataBackground, 175));
    drawFitted("UP NEXT", 164 + xOffset, 120, 140, &fonts::Font0,
               color == kText ? kViolet : blend565(kViolet, kBackground, 100));
    if (!playback_.queueAvailable) {
        drawFitted("QUEUE UNAVAILABLE", 164 + xOffset, 138, 136, &fonts::Font0, fadedArtist);
    } else if (playback_.queueCount == 0) {
        drawFitted("QUEUE EMPTY", 164 + xOffset, 138, 136, &fonts::Font0, fadedArtist);
    } else {
        for (std::uint8_t index = 0; index < std::min<std::uint8_t>(2, playback_.queueCount);
             ++index) {
            const auto& entry = playback_.queue[index];
            char label[290];
            if (entry.artist[0] == '\0') {
                std::snprintf(label, sizeof(label), "%u  %s", static_cast<unsigned>(index + 1),
                              entry.title[0] == '\0' ? "Untitled" : entry.title);
            } else {
                std::snprintf(label, sizeof(label), "%u  %s - %s", static_cast<unsigned>(index + 1),
                              entry.title[0] == '\0' ? "Untitled" : entry.title, entry.artist);
            }
            drawFitted(label, 164 + xOffset, 132 + index * 11, 136, &fonts::Font0, fadedArtist);
        }
    }
}

void UiController::drawTransportIcon(const std::int32_t centerX, const std::int32_t centerY,
                                     const app::PlaybackStatus status, const std::uint16_t color,
                                     const std::uint8_t pulse) {
    const auto radius = 13 + pulse;
    display_.fillCircle(centerX, centerY, radius, kSpotify);
    display_.drawCircle(centerX, centerY, radius, pulse == 0 ? kAccentDim : kAccent);
    if (status == app::PlaybackStatus::Playing) {
        display_.fillRect(centerX - 4, centerY - 6, 3, 12, color);
        display_.fillRect(centerX + 2, centerY - 6, 3, 12, color);
    } else {
        display_.fillTriangle(centerX - 4, centerY - 7, centerX - 4, centerY + 7, centerX + 7,
                              centerY, color);
    }
}

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

## FittedText primitive
- File: `firmware/src/ui/ui_controller.cpp:1183`
- Component: `drawFitted`
- Description: Complete fixed-width bitmap text helper with UTF-8-safe tail trimming and ellipsis.

```cpp
void UiController::drawFitted(const char* text, const std::int32_t x, const std::int32_t y,
                              const std::int32_t maxWidth, const lgfx::IFont* font,
                              const std::uint16_t color, const lgfx::textdatum_t datum) {
    char buffer[160];
    app::copyText(buffer, text == nullptr ? "" : text);
    display_.setFont(font);
    display_.setTextColor(color);
    display_.setTextDatum(datum);
    auto length = std::strlen(buffer);
    if (display_.textWidth(buffer) > maxWidth && length > 3) {
        while (length > 3 && display_.textWidth(buffer) > maxWidth) {
            --length;
            while (length > 0 && (static_cast<unsigned char>(buffer[length]) & 0xC0U) == 0x80U) {
                --length;
            }
            buffer[length] = '\0';
        }
        if (length > 3) {
            buffer[length - 3] = '.';
            buffer[length - 2] = '.';
            buffer[length - 1] = '.';
        }
    }
    display_.drawString(buffer, x, y);
}

}  // namespace deskwave::ui
```

## ProgressClock declaration
- File: `firmware/lib/deskwave_core/src/deskwave/core/progress.h`
- Component: `ProgressClock`
- Description: Complete monotonic progress primitive used by ProgressBand.

```cpp
#pragma once

#include <cstdint>

namespace deskwave::core {

class ProgressClock {
   public:
    void synchronize(std::uint64_t positionMs, std::uint64_t durationMs, bool playing,
                     std::uint32_t localNowMs) noexcept;
    void seek(std::int64_t offsetMs, std::uint32_t localNowMs) noexcept;
    [[nodiscard]] std::uint64_t position(std::uint32_t localNowMs) const noexcept;
    [[nodiscard]] std::uint64_t duration() const noexcept;
    [[nodiscard]] float fraction(std::uint32_t localNowMs) const noexcept;

   private:
    std::uint64_t positionMs_{0};
    std::uint64_t durationMs_{0};
    std::uint32_t synchronizedAtMs_{0};
    bool playing_{false};
};

}  // namespace deskwave::core
```

## ProgressClock implementation
- File: `firmware/lib/deskwave_core/src/deskwave/core/progress.cpp`
- Component: `ProgressClock` implementation
- Description: Complete bounded synchronize, seek, position, duration, and fraction behavior.

```cpp
#include "deskwave/core/progress.h"

#include <algorithm>

namespace deskwave::core {

void ProgressClock::synchronize(const std::uint64_t positionMs, const std::uint64_t durationMs,
                                const bool playing, const std::uint32_t localNowMs) noexcept {
    durationMs_ = durationMs;
    positionMs_ = durationMs_ == 0 ? positionMs : std::min(positionMs, durationMs_);
    playing_ = playing;
    synchronizedAtMs_ = localNowMs;
}

std::uint64_t ProgressClock::position(const std::uint32_t localNowMs) const noexcept {
    std::uint64_t result = positionMs_;
    if (playing_) {
        result += static_cast<std::uint32_t>(localNowMs - synchronizedAtMs_);
    }
    return durationMs_ == 0 ? result : std::min(result, durationMs_);
}

void ProgressClock::seek(const std::int64_t offsetMs, const std::uint32_t localNowMs) noexcept {
    const auto current = position(localNowMs);
    std::uint64_t target = 0;
    if (offsetMs >= 0) {
        target = current + static_cast<std::uint64_t>(offsetMs);
        if (durationMs_ != 0) {
            target = std::min(target, durationMs_);
        }
    } else {
        const auto magnitude = static_cast<std::uint64_t>(-(offsetMs + 1)) + 1;
        target = magnitude > current ? 0 : current - magnitude;
    }
    positionMs_ = target;
    synchronizedAtMs_ = localNowMs;
}

std::uint64_t ProgressClock::duration() const noexcept { return durationMs_; }

float ProgressClock::fraction(const std::uint32_t localNowMs) const noexcept {
    if (durationMs_ == 0) {
        return 0.0F;
    }
    return static_cast<float>(position(localNowMs)) / static_cast<float>(durationMs_);
}

}  // namespace deskwave::core
```
