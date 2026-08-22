#include "ui/ui_controller.h"

#include <LittleFS.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "deskwave_version.h"
#include "system/logging.h"

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
constexpr std::uint32_t kTitleFrameMs = 45;
constexpr std::uint32_t kTitleEdgeHoldMs = 700;
constexpr std::uint32_t kTitlePixelsPerSecond = 32;

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

const char* playbackStatusName(const app::PlaybackStatus status) {
    switch (status) {
        case app::PlaybackStatus::Playing:
            return "Playing";
        case app::PlaybackStatus::Paused:
            return "Paused";
        case app::PlaybackStatus::Stopped:
            return "Stopped";
    }
    return "Stopped";
}

const char* repeatName(const app::RepeatMode repeat) {
    switch (repeat) {
        case app::RepeatMode::Off:
            return "Off";
        case app::RepeatMode::Track:
            return "Track";
        case app::RepeatMode::Playlist:
            return "Playlist";
        case app::RepeatMode::Unknown:
            return "Unavailable";
    }
    return "Unavailable";
}

const char* defaultScreenName(const std::uint8_t screen) {
    switch (screen) {
        case 0:
            return "Now Playing";
        case 1:
            return "Device";
        case 2:
            return "Settings";
        case 3:
            return "About";
        default:
            return "Now Playing";
    }
}

void formatDuration(const std::uint64_t milliseconds, char (&buffer)[16]) {
    const auto totalSeconds = milliseconds / 1'000;
    const auto minutes =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(totalSeconds / 60, 999'999));
    const auto seconds = static_cast<std::uint32_t>(totalSeconds % 60);
    std::snprintf(buffer, sizeof(buffer), "%lu:%02lu", static_cast<unsigned long>(minutes),
                  static_cast<unsigned long>(seconds));
}

}  // namespace

UiController::UiController(display::DisplayDriver& display) : display_(display) {}

bool UiController::begin(const std::uint8_t brightness, const std::uint8_t defaultScreen,
                         const std::uint32_t nowMs) {
    if (!display_.initialize(brightness)) {
        return false;
    }
    screen_ = defaultScreen <= static_cast<std::uint8_t>(Screen::About)
                  ? static_cast<Screen>(defaultScreen)
                  : Screen::NowPlaying;
    bootUntilMs_ = nowMs + 900;
    renderBoot();
    bootRendered_ = true;
    dirty_ = true;
    return true;
}

Screen UiController::screen() const noexcept { return screen_; }

core::ControlContext UiController::controlContext() const noexcept {
    switch (screen_) {
        case Screen::Actions:
            return core::ControlContext::Actions;
        case Screen::Device:
            return core::ControlContext::Device;
        case Screen::Settings:
            return core::ControlContext::Settings;
        case Screen::NowPlaying:
        case Screen::About:
            return core::ControlContext::Playback;
    }
    return core::ControlContext::Playback;
}

const char* UiController::screenName(const Screen screen) noexcept {
    switch (screen) {
        case Screen::NowPlaying:
            return "NOW PLAYING";
        case Screen::Device:
            return "DEVICE";
        case Screen::Settings:
            return "SETTINGS";
        case Screen::About:
            return "ABOUT";
        case Screen::Actions:
            return "ACTIONS";
    }
    return "DESKWAVE";
}

void UiController::nextScreen() {
    switch (screen_) {
        case Screen::NowPlaying:
            screen_ = Screen::Device;
            break;
        case Screen::Device:
            screen_ = Screen::Settings;
            break;
        case Screen::Settings:
            screen_ = Screen::About;
            break;
        case Screen::About:
        case Screen::Actions:
            screen_ = Screen::NowPlaying;
            break;
    }
    volumeOverlayUntilMs_ = 0;
    toastUntilMs_ = 0;
    dirty_ = true;
}

void UiController::toggleActions() {
    if (screen_ == Screen::Actions) {
        screen_ = screenBeforeActions_;
    } else {
        screenBeforeActions_ = screen_;
        screen_ = Screen::Actions;
    }
    dirty_ = true;
}

void UiController::setBrightness(const std::uint8_t brightness) {
    dimmed_ = false;
    display_.setBrightness(brightness);
}

void UiController::setDimmed(const bool dimmed, const std::uint8_t normalBrightness) {
    if (dimmed_ == dimmed) {
        return;
    }
    dimmed_ = dimmed;
    const auto dimBrightness = static_cast<std::uint8_t>(std::max<int>(10, normalBrightness / 5));
    display_.setBrightness(dimmed ? dimBrightness : normalBrightness);
}

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
                                 std::strcmp(playback_.artist, snapshot.artist) != 0;
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
    players_ = players;
    selectedPlayer_ =
        players_.count == 0 ? 0 : std::min<std::uint8_t>(selectedPlayer, players_.count - 1);
    if (screen_ == Screen::Device) {
        dirty_ = true;
    }
}

void UiController::setSelectedPlayer(const std::uint8_t selectedPlayer) {
    selectedPlayer_ =
        players_.count == 0 ? 0 : std::min<std::uint8_t>(selectedPlayer, players_.count - 1);
    if (screen_ == Screen::Device) {
        dirty_ = true;
    }
}

void UiController::setDeviceStatus(const DeviceStatus& status) {
    const bool headerChanged = status_.hostConnected != status.hostConnected ||
                               status_.wifiConnected != status.wifiConnected ||
                               status_.state != status.state;
    status_ = status;
    if (screen_ == Screen::Device || screen_ == Screen::About || headerChanged) {
        dirty_ = true;
    }
}

void UiController::setNotice(const app::SystemNotice& notice, const std::uint32_t nowMs) {
    latestNotice_ = notice;
    status_.state = notice.state;
    if (notice.type == app::SystemNoticeType::RecoverableError ||
        notice.type == app::SystemNoticeType::CommandFailed) {
        showToast(notice.primary, nowMs, true);
        return;
    }
    if (notice.type == app::SystemNoticeType::StateChanged ||
        notice.type == app::SystemNoticeType::PairingCode ||
        notice.type == app::SystemNoticeType::ProvisioningStarted) {
        app::copyText(status_.stateLabel, notice.primary);
        app::copyText(status_.stateDetail, notice.secondary);
        dirty_ = true;
    }
}

void UiController::setSettings(const SettingsView& settings) {
    settings_ = settings;
    if (screen_ == Screen::Settings) {
        dirty_ = true;
    }
}

void UiController::showVolume(const std::int16_t percent, const bool muted,
                              const std::uint32_t nowMs) {
    volumeOverlayPercent_ = std::clamp<std::int16_t>(percent, 0, 100);
    volumeOverlayMuted_ = muted;
    volumeOverlayUntilMs_ = nowMs + kOverlayDurationMs;
    toastUntilMs_ = 0;
    lastAnimationFrameMs_ = 0;
}

void UiController::showTransport(const app::PlaybackStatus status, const std::uint32_t nowMs) {
    const auto current = progress_.position(nowMs);
    playback_.status = status;
    progress_.synchronize(current, progress_.duration(), status == app::PlaybackStatus::Playing,
                          nowMs);
    transportPulseUntilMs_ = nowMs + 360;
    if (screen_ == Screen::NowPlaying && volumeOverlayUntilMs_ == 0) {
        renderFooter(nowMs);
    }
}

void UiController::optimisticSeek(const std::int32_t offsetMs, const std::uint32_t nowMs) {
    progress_.seek(offsetMs, nowMs);
    char message[32];
    std::snprintf(message, sizeof(message), "Seek %c%ld sec", offsetMs >= 0 ? '+' : '-',
                  static_cast<long>(std::abs(offsetMs) / 1'000));
    showToast(message, nowMs, false);
}

void UiController::showToast(const char* message, const std::uint32_t nowMs, const bool error) {
    app::copyText(toast_, message);
    toastError_ = error;
    toastUntilMs_ = nowMs + kToastDurationMs;
    volumeOverlayUntilMs_ = 0;
    lastAnimationFrameMs_ = 0;
}

void UiController::showFactoryResetChord(const std::uint8_t secondsRemaining) {
    if (!factoryResetChordVisible_ || resetSecondsRemaining_ != secondsRemaining) {
        factoryResetChordVisible_ = true;
        resetSecondsRemaining_ = secondsRemaining;
        dirty_ = true;
    }
}

void UiController::hideFactoryResetChord() {
    if (factoryResetChordVisible_) {
        factoryResetChordVisible_ = false;
        dirty_ = true;
    }
}

void UiController::showResetting() {
    display_.fillScreen(kBackground);
    display_.fillCircle(160, 91, 30, kAccentDim);
    display_.drawCircle(160, 91, 30, kAccent);
    display_.drawLine(148, 91, 157, 100, kText);
    display_.drawLine(157, 100, 175, 80, kText);
    drawFitted("Factory reset complete", 160, 137, 280, &fonts::Font2, kText,
               lgfx::textdatum_t::top_center);
    drawFitted("Restarting DeskWave...", 160, 164, 280, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);
    dirty_ = false;
}

bool UiController::connectionScreenActive() const noexcept {
    if (status_.state == core::SystemState::Ready || status_.state == core::SystemState::Playing ||
        status_.state == core::SystemState::Paused) {
        return false;
    }
    // Once a real snapshot exists, retain it during host rediscovery instead
    // of replacing the useful controller with a repeating offline page.
    if (hasPlayback_ && (status_.state == core::SystemState::DiscoveringHost ||
                         status_.state == core::SystemState::ConnectingHost)) {
        return false;
    }
    return true;
}

void UiController::tick(const std::uint32_t nowMs) {
    if (bootRendered_ && static_cast<std::int32_t>(nowMs - bootUntilMs_) < 0) {
        return;
    }
    if (bootRendered_) {
        bootRendered_ = false;
        dirty_ = true;
    }
    if (hasPendingPlayback_) {
        tickTrackTransition(nowMs);
    }
    if (volumeOverlayUntilMs_ != 0 &&
        static_cast<std::int32_t>(nowMs - volumeOverlayUntilMs_) >= 0) {
        volumeOverlayUntilMs_ = 0;
        dirty_ = true;
    }
    if (toastUntilMs_ != 0 && static_cast<std::int32_t>(nowMs - toastUntilMs_) >= 0) {
        toastUntilMs_ = 0;
        dirty_ = true;
    }
    if (dirty_) {
        render(nowMs);
        dirty_ = false;
        lastAnimationFrameMs_ = nowMs;
        lastProgressFrameMs_ = nowMs;
        return;
    }
    if ((volumeOverlayUntilMs_ != 0 || toastUntilMs_ != 0) &&
        static_cast<std::uint32_t>(nowMs - lastAnimationFrameMs_) >= 80) {
        if (volumeOverlayUntilMs_ != 0) {
            renderVolumeOverlay(nowMs);
        } else {
            renderToast();
        }
        lastAnimationFrameMs_ = nowMs;
        return;
    }
    bool animatedNowPlaying = false;
    if (screen_ == Screen::NowPlaying && !connectionScreenActive() && !hasPendingPlayback_ &&
        volumeOverlayUntilMs_ == 0 && toastUntilMs_ == 0 && !factoryResetChordVisible_) {
        if (titleScrollActive_ &&
            static_cast<std::uint32_t>(nowMs - lastTitleFrameMs_) >= kTitleFrameMs) {
            renderTitle(nowMs, kText);
            lastTitleFrameMs_ = nowMs;
            animatedNowPlaying = true;
        }
        if (static_cast<std::uint32_t>(nowMs - lastProgressFrameMs_) >= kProgressFrameMs) {
            renderProgress(nowMs);
            lastProgressFrameMs_ = nowMs;
            animatedNowPlaying = true;
        }
    }
    if (animatedNowPlaying) {
        return;
    }
    if (connectionScreenActive() &&
        static_cast<std::uint32_t>(nowMs - lastAnimationFrameMs_) >= 250) {
        renderHeader(nowMs);
        lastAnimationFrameMs_ = nowMs;
    }
}

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

void UiController::render(const std::uint32_t nowMs) {
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
    display_.fillCircle(272, 15, 3, connectionColor);
    drawFitted(status_.hostConnected ? "LINKED" : "RETRY", 312, 10, 34, &fonts::Font0,
               connectionColor, lgfx::textdatum_t::top_right);
}

void UiController::renderConnection(const std::uint32_t nowMs) {
    (void)nowMs;
    display_.fillRoundRect(20, 43, 280, 174, 16, blend565(kPanel, kBackground, 230));
    display_.drawRoundRect(20, 43, 280, 174, 16, blend565(kViolet, kLine, 120));
    display_.drawCircle(160, 86, 31, blend565(kAccent, kBackground, 82));
    display_.drawCircle(160, 86, 23, blend565(kViolet, kBackground, 90));
    const bool pairing = latestNotice_.type == app::SystemNoticeType::PairingCode &&
                         status_.state == core::SystemState::Pairing;
    const bool provisioning = latestNotice_.type == app::SystemNoticeType::ProvisioningStarted &&
                              status_.state == core::SystemState::Provisioning;
    if (pairing) {
        drawFitted("PAIRING CODE", 160, 61, 250, &fonts::Font2, kTextMuted,
                   lgfx::textdatum_t::top_center);
        drawFitted(latestNotice_.primary, 160, 91, 250, &fonts::AsciiFont24x48, kAccent,
                   lgfx::textdatum_t::top_center);
        drawFitted("On the host, run:", 160, 153, 250, &fonts::Font0, kTextMuted,
                   lgfx::textdatum_t::top_center);
        drawFitted("deskwave-host pair CODE", 160, 171, 250, &fonts::Font2, kText,
                   lgfx::textdatum_t::top_center);
        return;
    }
    if (provisioning) {
        drawFitted("WI-FI SETUP", 160, 58, 250, &fonts::Font2, kAccent,
                   lgfx::textdatum_t::top_center);
        drawFitted("Connect a phone or laptop to", 160, 88, 250, &fonts::Font0, kTextMuted,
                   lgfx::textdatum_t::top_center);
        drawFitted(latestNotice_.primary, 160, 108, 250, &fonts::Font2, kText,
                   lgfx::textdatum_t::top_center);
        drawFitted("Password", 160, 139, 250, &fonts::Font0, kTextMuted,
                   lgfx::textdatum_t::top_center);
        drawFitted(latestNotice_.secondary, 160, 157, 250, &fonts::Font2, kWarning,
                   lgfx::textdatum_t::top_center);
        drawFitted("Then open http://192.168.4.1", 160, 188, 250, &fonts::Font0, kTextMuted,
                   lgfx::textdatum_t::top_center);
        return;
    }
    const char* primary = status_.stateLabel[0] == '\0' ? "Starting DeskWave" : status_.stateLabel;
    drawFitted(primary, 160, 112, 248, &fonts::Font4,
               status_.state == core::SystemState::Error ? kError : kText,
               lgfx::textdatum_t::top_center);
    if (status_.stateDetail[0] != '\0') {
        drawFitted(status_.stateDetail, 160, 146, 250, &fonts::Font2, kTextMuted,
                   lgfx::textdatum_t::top_center);
    }
    const auto phase = static_cast<std::uint8_t>((millis() / 320U) % 4U);
    for (std::uint8_t index = 0; index < 4; ++index) {
        display_.fillCircle(139 + index * 14, 183, 3,
                            index == phase ? kAccent : blend565(kAccent, kPanel, 60));
    }
    drawFitted("Connection retries automatically", 160, 201, 250, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);
}

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
        rendered = display_.drawJpgFile(
            LittleFS, artworkPath_, kArtworkX, kArtworkY, kArtworkSize, kArtworkSize, 0, 0,
            static_cast<float>(kArtworkSize) / 240.0F, static_cast<float>(kArtworkSize) / 240.0F,
            lgfx::textdatum_t::top_left);
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
        const auto travel = static_cast<std::uint32_t>(titleTextWidth_ - kTitleWidth);
        const auto moveDuration = std::clamp<std::uint32_t>(
            travel * 1'000U / kTitlePixelsPerSecond, 1'200U, 8'000U);
        const auto cycleDuration = 2U * (kTitleEdgeHoldMs + moveDuration);
        const auto phase = static_cast<std::uint32_t>(nowMs - titleScrollStartedAtMs_) %
                           cycleDuration;

        if (phase >= kTitleEdgeHoldMs && phase < kTitleEdgeHoldMs + moveDuration) {
            const auto elapsed = phase - kTitleEdgeHoldMs;
            const auto normalized = static_cast<std::uint32_t>(elapsed * 1'024ULL / moveDuration);
            const auto eased = static_cast<std::uint32_t>(
                normalized * normalized * (3'072ULL - 2ULL * normalized) / (1'024ULL * 1'024ULL));
            scrollOffset = -static_cast<std::int32_t>(travel * eased / 1'024U);
        } else if (phase < 2U * kTitleEdgeHoldMs + moveDuration) {
            scrollOffset = -static_cast<std::int32_t>(travel);
        } else {
            const auto elapsed = phase - (2U * kTitleEdgeHoldMs + moveDuration);
            const auto normalized = static_cast<std::uint32_t>(elapsed * 1'024ULL / moveDuration);
            const auto eased = static_cast<std::uint32_t>(
                normalized * normalized * (3'072ULL - 2ULL * normalized) / (1'024ULL * 1'024ULL));
            scrollOffset = -static_cast<std::int32_t>(travel) +
                           static_cast<std::int32_t>(travel * eased / 1'024U);
        }
    }

    const auto metadataBackground = blend565(kPanel, kBackground, 235);
    display_.setClipRect(kTitleX, kTitleY, kTitleWidth, kTitleHeight);
    display_.fillRect(kTitleX, kTitleY, kTitleWidth, kTitleHeight, metadataBackground);
    display_.setFont(&fonts::Font4);
    display_.setTextColor(color);
    display_.setTextDatum(lgfx::textdatum_t::top_left);
    display_.drawString(title, kTitleX + xOffset + scrollOffset, kTitleY + 1);
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
    drawFitted("UP NEXT", 164 + xOffset, 122, 70, &fonts::Font0,
               color == kText ? kViolet : blend565(kViolet, kBackground, 100));
    display_.drawCircle(167 + xOffset, 143, 3, fadedArtist);
    drawFitted("QUEUE NOT SHARED", 176 + xOffset, 138, 120, &fonts::Font0, fadedArtist);
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
    display_.fillRect(0, 174, 320, 21, footerBackground);
    display_.fillRoundRect(8, 178, 304, 3, 1, kLine);
    const auto progressWidth = static_cast<std::int32_t>(progress_.fraction(nowMs) * 304.0F);
    if (progressWidth > 0) {
        display_.fillRoundRect(8, 178, progressWidth, 3, 1, kAccent);
        display_.fillCircle(8 + progressWidth, 179, 3, kText);
    }
    char position[16];
    char duration[16];
    formatDuration(progress_.position(nowMs), position);
    formatDuration(progress_.duration(), duration);
    drawFitted(position, 8, 184, 60, &fonts::Font0, kTextMuted);
    drawFitted(playback_.hasDuration ? duration : "--:--", 312, 184, 60, &fonts::Font0,
               kTextMuted,
               lgfx::textdatum_t::top_right);
}

void UiController::renderDevice() {
    display_.fillRoundRect(10, 35, 300, 76, 10, kPanel);
    drawFitted("NETWORK", 22, 45, 90, &fonts::Font0, kTextMuted);
    drawFitted(status_.wifiConnected ? status_.ssid : "Wi-Fi unavailable", 22, 63, 172,
               &fonts::Font2, status_.wifiConnected ? kText : kWarning);
    drawFitted(status_.wifiConnected ? status_.ipAddress : "Reconnecting", 22, 87, 180,
               &fonts::Font0, kTextMuted);
    char signal[24];
    std::snprintf(signal, sizeof(signal), "%ld dBm", static_cast<long>(status_.rssi));
    drawFitted(status_.wifiConnected ? signal : "OFFLINE", 298, 65, 85, &fonts::Font2,
               status_.wifiConnected ? kAccent : kWarning, lgfx::textdatum_t::top_right);

    drawFitted("PLAYBACK DEVICES", 12, 124, 160, &fonts::Font0, kTextMuted);
    if (players_.count == 0) {
        display_.fillRoundRect(10, 142, 300, 58, 9, kPanel);
        drawFitted("No MPRIS players detected", 160, 159, 270, &fonts::Font2, kTextMuted,
                   lgfx::textdatum_t::top_center);
    } else {
        std::uint8_t first = selectedPlayer_ > 1 ? selectedPlayer_ - 1 : 0;
        if (players_.count > 3 && first + 3 > players_.count) {
            first = players_.count - 3;
        }
        const auto visible = std::min<std::uint8_t>(3, players_.count);
        for (std::uint8_t row = 0; row < visible; ++row) {
            const auto index = static_cast<std::uint8_t>(first + row);
            const auto y = 141 + row * 26;
            const bool selected = index == selectedPlayer_;
            display_.fillRoundRect(10, y, 300, 23, 6, selected ? kAccentDim : kPanel);
            drawFitted(selected ? ">" : "", 18, y + 7, 12, &fonts::Font0, kAccent);
            drawFitted(players_.players[index].name, 34, y + 4, 196, &fonts::Font2,
                       selected ? kText : kTextMuted);
            drawFitted(playbackStatusName(players_.players[index].status), 298, y + 7, 64,
                       &fonts::Font0,
                       players_.players[index].status == app::PlaybackStatus::Playing ? kAccent
                                                                                      : kTextMuted,
                       lgfx::textdatum_t::top_right);
        }
    }
    drawFitted("Turn: choose   Press: activate   MENU: next", 160, 222, 300, &fonts::Font0,
               kTextMuted, lgfx::textdatum_t::top_center);
}

void UiController::renderActions() {
    drawFitted("Shape the sound", 18, 39, 280, &fonts::Font4, kText);
    drawFitted("Tap a luminous card", 19, 67, 260, &fonts::Font0, kTextMuted);
    display_.fillRoundRect(12, 82, 142, 98, 14, blend565(kPanel, kBackground, 235));
    display_.fillRoundRect(166, 82, 142, 98, 14, blend565(kPanel, kBackground, 235));
    display_.drawRoundRect(12, 82, 142, 98, 14,
                           playback_.shuffleKnown && playback_.shuffle ? kAccent : kLine);
    display_.drawRoundRect(166, 82, 142, 98, 14,
                           playback_.repeat == app::RepeatMode::Off ? kLine : kViolet);
    drawFitted("SHUFFLE", 83, 93, 125, &fonts::Font0, kTextMuted, lgfx::textdatum_t::top_center);
    drawFitted(playback_.shuffleKnown ? (playback_.shuffle ? "ON" : "OFF") : "N/A", 83, 119, 125,
               &fonts::Font4, playback_.shuffleKnown && playback_.shuffle ? kAccent : kText,
               lgfx::textdatum_t::top_center);
    drawFitted("SMART*", 83, 157, 90, &fonts::Font0, kTextMuted, lgfx::textdatum_t::top_center);
    drawFitted("REPEAT", 237, 93, 125, &fonts::Font0, kTextMuted, lgfx::textdatum_t::top_center);
    drawFitted(repeatName(playback_.repeat), 237, 119, 125, &fonts::Font2,
               playback_.repeat == app::RepeatMode::Unknown ? kTextMuted : kAccent,
               lgfx::textdatum_t::top_center);
    drawFitted("OFF / ONE / ALL", 237, 157, 120, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);
    drawFitted("*Smart Shuffle is not exposed by this player", 160, 184, 294, &fonts::Font0,
               kTextMuted, lgfx::textdatum_t::top_center);
    const auto footerBackground = blend565(kPanel, kBackground, 225);
    display_.fillRect(0, 195, 320, 45, footerBackground);
    display_.fillRect(0, 195, 320, 1, blend565(kLine, kBackground, 190));
    drawFitted("ACTIONS", 14, 205, 100, &fonts::Font2, kText);
    drawFitted("Tap a card or close", 14, 224, 190, &fonts::Font0, kTextMuted);
    display_.drawFastVLine(256, 199, 36, blend565(kLine, footerBackground, 175));
    display_.fillCircle(280, 210, 2, kViolet);
    display_.fillCircle(288, 210, 2, kViolet);
    display_.fillCircle(296, 210, 2, kViolet);
    drawFitted("CLOSE", 288, 227, 58, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);
}

void UiController::renderSettings() {
    constexpr std::uint8_t kItemCount = 5;
    const char* labels[kItemCount] = {"Brightness", "Idle dim", "Volume step", "Default screen",
                                      "Factory reset"};
    char values[kItemCount][24]{};
    std::snprintf(values[0], sizeof(values[0]), "%u%%",
                  static_cast<unsigned>((settings_.brightness * 100U) / 255U));
    if (settings_.dimTimeoutSeconds == 0) {
        std::snprintf(values[1], sizeof(values[1]), "Off");
    } else if (settings_.dimTimeoutSeconds < 60) {
        std::snprintf(values[1], sizeof(values[1]), "%lu sec",
                      static_cast<unsigned long>(settings_.dimTimeoutSeconds));
    } else {
        std::snprintf(values[1], sizeof(values[1]), "%lu min",
                      static_cast<unsigned long>(settings_.dimTimeoutSeconds / 60));
    }
    std::snprintf(values[2], sizeof(values[2]), "%u%%",
                  static_cast<unsigned>(settings_.volumeStepPercent));
    std::snprintf(values[3], sizeof(values[3]), "%s", defaultScreenName(settings_.defaultScreen));
    std::snprintf(values[4], sizeof(values[4]), "%s",
                  settings_.factoryResetConfirmation ? "Hold knob" : "Select");

    for (std::uint8_t index = 0; index < kItemCount; ++index) {
        const auto y = 36 + index * 35;
        const bool selected = index == settings_.selectedItem;
        display_.fillRoundRect(10, y, 300, 30, 7, selected ? kAccentDim : kPanel);
        if (selected) {
            display_.fillRoundRect(10, y, 4, 30, 2, kAccent);
        }
        drawFitted(labels[index], 23, y + 8, 145, &fonts::Font2, selected ? kText : kTextMuted);
        drawFitted(values[index], 296, y + 8, 130, &fonts::Font2,
                   index == 4 && settings_.factoryResetConfirmation
                       ? kWarning
                       : (selected ? kAccent : kTextMuted),
                   lgfx::textdatum_t::top_right);
    }
    drawFitted("Turn: select   LEFT/RIGHT: change", 160, 216, 300, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);
    drawFitted("MENU: next", 160, 229, 200, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);
}

void UiController::renderAbout() {
    drawFitted("DeskWave", 18, 39, 180, &fonts::Font4, kText);
    char version[32];
    std::snprintf(version, sizeof(version), "Firmware v%s", deskwave::kVersion);
    drawFitted(version, 19, 75, 180, &fonts::Font2, kAccent);
    drawFitted("Protocol 1", 19, 98, 180, &fonts::Font2, kTextMuted);

    display_.fillRoundRect(10, 128, 300, 79, 10, kPanel);
    char uptime[32];
    char heap[32];
    char largest[32];
    std::snprintf(uptime, sizeof(uptime), "%luh %02lum",
                  static_cast<unsigned long>(status_.uptimeSeconds / 3'600),
                  static_cast<unsigned long>((status_.uptimeSeconds / 60) % 60));
    std::snprintf(heap, sizeof(heap), "%lu KB",
                  static_cast<unsigned long>(status_.freeHeap / 1024));
    std::snprintf(largest, sizeof(largest), "%lu KB",
                  static_cast<unsigned long>(status_.largestFreeBlock / 1024));
    drawFitted("IP", 22, 139, 70, &fonts::Font0, kTextMuted);
    drawFitted(status_.wifiConnected ? status_.ipAddress : "Offline", 298, 137, 200, &fonts::Font2,
               kText, lgfx::textdatum_t::top_right);
    drawFitted("Uptime", 22, 160, 70, &fonts::Font0, kTextMuted);
    drawFitted(uptime, 138, 158, 100, &fonts::Font2, kText, lgfx::textdatum_t::top_right);
    drawFitted("Heap", 155, 160, 55, &fonts::Font0, kTextMuted);
    drawFitted(heap, 298, 158, 85, &fonts::Font2, kText, lgfx::textdatum_t::top_right);
    drawFitted("Largest block", 22, 184, 95, &fonts::Font0, kTextMuted);
    drawFitted(largest, 298, 181, 90, &fonts::Font2, kText, lgfx::textdatum_t::top_right);
    drawFitted("Open-source hardware companion", 160, 217, 280, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);
}

void UiController::renderVolumeOverlay(const std::uint32_t nowMs) {
    std::uint8_t opacity = 255;
    if (volumeOverlayUntilMs_ > nowMs && volumeOverlayUntilMs_ - nowMs < 300) {
        opacity = static_cast<std::uint8_t>((volumeOverlayUntilMs_ - nowMs) * 255U / 300U);
    }
    const auto panelColor =
        blend565(kPanelRaised, kBackground, std::max<std::uint8_t>(opacity, 80));
    display_.fillRoundRect(76, 65, 168, 108, 13, panelColor);
    display_.drawRoundRect(76, 65, 168, 108, 13, blend565(kAccent, panelColor, opacity));
    drawFitted(volumeOverlayMuted_ ? "MUTED" : "VOLUME", 160, 79, 140, &fonts::Font2,
               blend565(volumeOverlayMuted_ ? kWarning : kText, panelColor, opacity),
               lgfx::textdatum_t::top_center);
    display_.fillRoundRect(94, 112, 132, 10, 4, kLine);
    const auto width = static_cast<std::int32_t>(volumeOverlayPercent_ * 132 / 100);
    if (width > 0 && !volumeOverlayMuted_) {
        display_.fillRoundRect(94, 112, width, 10, 4, blend565(kAccent, panelColor, opacity));
    }
    char percent[16];
    std::snprintf(percent, sizeof(percent), "%d%%", volumeOverlayPercent_);
    drawFitted(percent, 160, 135, 110, &fonts::Font4, blend565(kText, panelColor, opacity),
               lgfx::textdatum_t::top_center);
}

void UiController::renderToast() {
    display_.fillRoundRect(24, 188, 272, 40, 9, kPanelRaised);
    display_.drawRoundRect(24, 188, 272, 40, 9, toastError_ ? kError : kAccentDim);
    drawFitted(toast_, 160, 201, 246, &fonts::Font2, toastError_ ? kError : kText,
               lgfx::textdatum_t::top_center);
}

void UiController::renderFactoryResetOverlay() {
    display_.fillRoundRect(40, 61, 240, 118, 14, kPanelRaised);
    display_.drawRoundRect(40, 61, 240, 118, 14, kError);
    drawFitted("FACTORY RESET", 160, 79, 210, &fonts::Font2, kError, lgfx::textdatum_t::top_center);
    drawFitted("Keep LEFT + RIGHT + MENU held", 160, 108, 214, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);
    char countdown[32];
    std::snprintf(countdown, sizeof(countdown), "%u",
                  static_cast<unsigned>(resetSecondsRemaining_));
    drawFitted(countdown, 160, 130, 80, &fonts::Font4, kWarning, lgfx::textdatum_t::top_center);
}

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
