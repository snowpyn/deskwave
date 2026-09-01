#include "ui/ui_controller.h"

#include <LittleFS.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "deskwave/core/clock.h"
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

constexpr std::int32_t kArtworkX = 6;
constexpr std::int32_t kArtworkY = 7;
constexpr std::int32_t kArtworkSize = 166;
constexpr std::int32_t kMetadataX = 180;
constexpr std::int32_t kMetadataY = 7;
constexpr std::int32_t kMetadataWidth = 134;
constexpr std::int32_t kMetadataHeight = 166;
constexpr std::int32_t kTitleX = 188;
constexpr std::int32_t kTitleY = 24;
constexpr std::int32_t kTitleWidth = 118;
constexpr std::int32_t kTitleHeight = 25;
constexpr std::int32_t kTrackLabelY = 12;
constexpr std::int32_t kArtistY = 57;
constexpr std::int32_t kQueueY = 88;
constexpr std::int32_t kQueueHeight = 76;
constexpr std::int32_t kMainBottomY = 178;
constexpr std::int32_t kProgressY = 184;
constexpr std::int32_t kProgressHeight = 21;
constexpr std::int32_t kControlsY = 205;
constexpr std::int32_t kControlsHeight = 35;
constexpr std::int32_t kTransportCenterY = 222;
constexpr std::uint32_t kTrackTransitionMs = 220;
constexpr std::uint32_t kOverlayDurationMs = 1'600;
constexpr std::uint32_t kToastDurationMs = 2'200;
constexpr std::uint32_t kProgressFrameMs = 500;
constexpr std::uint32_t kTitleFrameMs = 33;
constexpr std::uint32_t kTitlePixelsPerSecond = 34;
constexpr std::int32_t kTitleRepeatGap = 16;
constexpr std::uint32_t kThemeTransitionMs = 750;
constexpr std::uint32_t kRestTransitionMs = 600;
constexpr std::uint32_t kThemeFrameMs = 50;
constexpr std::uint32_t kIdleFrameMs = 50;
constexpr std::uint32_t kIdleHaloPeriodMs = 2'400;
constexpr std::int32_t kIdleTopBandHeight = 80;
constexpr std::int32_t kIdleBottomBandY = 174;
constexpr std::uint8_t kArtworkGlowStrength = 112;
constexpr std::uint8_t kRestGlowScale = 128;
constexpr std::uint8_t kRestDesaturation = 58;
constexpr std::uint8_t kRestBackgroundBlend = 42;

constexpr core::ThemePalette kFallbackTheme{
    {111, 218, 194},
    {168, 145, 222},
    {7, 10, 18},
    {241, 244, 242},
};

bool needsJapaneseFont(const char* text) noexcept {
    if (text == nullptr) {
        return false;
    }
    while (*text != '\0') {
        if ((static_cast<unsigned char>(*text) & 0x80U) != 0) {
            return true;
        }
        ++text;
    }
    return false;
}

const lgfx::IFont* fontForText(const char* text, const lgfx::IFont* latinFont) noexcept {
    return needsJapaneseFont(text) ? &fonts::lgfxJapanGothicP_12 : latinFont;
}

constexpr std::uint16_t rgb565(const core::Rgb888 color) {
    return rgb565(color.red, color.green, color.blue);
}

constexpr core::Rgb888 rgb888(const std::uint16_t color) {
    const auto red = static_cast<std::uint8_t>((color >> 11U) & 0x1FU);
    const auto green = static_cast<std::uint8_t>((color >> 5U) & 0x3FU);
    const auto blue = static_cast<std::uint8_t>(color & 0x1FU);
    return {
        static_cast<std::uint8_t>((red << 3U) | (red >> 2U)),
        static_cast<std::uint8_t>((green << 2U) | (green >> 4U)),
        static_cast<std::uint8_t>((blue << 3U) | (blue >> 2U)),
    };
}

std::uint8_t scaledAmount(const std::uint8_t amount, const std::uint8_t scale) {
    return static_cast<std::uint8_t>((static_cast<std::uint16_t>(amount) * scale + 127U) / 255U);
}

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

UiController::UiController(display::DisplayDriver& display)
    : display_(display), themeFrom_(kFallbackTheme), themeTarget_(kFallbackTheme) {}

core::ThemePalette UiController::sampledTheme(const std::uint32_t nowMs) const noexcept {
    if (core::themesEqual(themeFrom_, themeTarget_)) {
        return themeTarget_;
    }
    const auto amount = core::easedProgress(
        static_cast<std::uint32_t>(nowMs - themeTransitionStartedAtMs_), kThemeTransitionMs);
    return core::interpolateTheme(themeFrom_, themeTarget_, amount);
}

std::uint8_t UiController::sampledRestAmount(const std::uint32_t nowMs) const noexcept {
    if (restFrom_ == restTarget_) {
        return restTarget_;
    }
    const auto amount = core::easedProgress(
        static_cast<std::uint32_t>(nowMs - restTransitionStartedAtMs_), kRestTransitionMs);
    return static_cast<std::uint8_t>((static_cast<std::uint16_t>(restFrom_) * (255U - amount) +
                                      static_cast<std::uint16_t>(restTarget_) * amount + 127U) /
                                     255U);
}

UiController::RenderTheme UiController::renderTheme(const std::uint32_t nowMs) const noexcept {
    const auto palette = sampledTheme(nowMs);
    const auto rest = sampledRestAmount(nowMs);
    const auto desaturation = scaledAmount(kRestDesaturation, rest);
    const auto backgroundBlend = scaledAmount(kRestBackgroundBlend, rest);
    const auto primary =
        core::softenColor(palette.primary, palette.background, desaturation, backgroundBlend);
    const auto secondary =
        core::softenColor(palette.secondary, palette.background, desaturation, backgroundBlend);
    const auto glowScale = static_cast<std::uint8_t>(
        255U - (static_cast<std::uint16_t>(rest) * (255U - kRestGlowScale) + 127U) / 255U);
    return {rgb565(primary), rgb565(secondary), rgb565(palette.background),
            rgb565(palette.foreground), glowScale};
}

core::Rgb888 UiController::lightColor(const std::uint32_t nowMs) const noexcept {
    if (!hasPlayback_) {
        return {};
    }
    // Sample the final RGB565 canvas rather than the palette's brighter primary role.
    // This includes the same track interpolation and resting-state softening that the
    // display renders, and expanding that actual panel value avoids a second color path
    // drifting from the visible song background through rounding or blend changes.
    return rgb888(canvasBackground(renderTheme(nowMs)));
}

std::uint16_t UiController::canvasBackground(const RenderTheme& theme) const noexcept {
    // The host's dark-support role may be near-neutral for a cover dominated by black.
    // Seed it with a restrained primary tint so a black/red cover reads as oxblood rather
    // than generic gray, then cap luminance for this small always-on display.
    const auto artworkTint = blend565(theme.primary, theme.background, 96);
    return blend565(artworkTint, kBackground, 220);
}

std::uint16_t UiController::panelBackground(const RenderTheme& theme) const noexcept {
    return blend565(kPanel, canvasBackground(theme), 42);
}

std::uint16_t UiController::raisedPanelBackground(const RenderTheme& theme) const noexcept {
    return blend565(kPanelRaised, canvasBackground(theme), 58);
}

bool UiController::themeAnimating(const std::uint32_t nowMs) const noexcept {
    const bool paletteActive = !core::themesEqual(themeFrom_, themeTarget_) &&
                               static_cast<std::uint32_t>(nowMs - themeTransitionStartedAtMs_) <
                                   kThemeTransitionMs + kThemeFrameMs;
    const bool restActive =
        restFrom_ != restTarget_ && static_cast<std::uint32_t>(nowMs - restTransitionStartedAtMs_) <
                                        kRestTransitionMs + kThemeFrameMs;
    return paletteActive || restActive;
}

void UiController::transitionTheme(const core::ThemePalette& theme, const std::uint32_t nowMs) {
    if (core::themesEqual(themeTarget_, theme)) {
        return;
    }
    themeFrom_ = sampledTheme(nowMs);
    themeTarget_ = theme;
    themeTransitionStartedAtMs_ = nowMs;
    lastThemeFrameMs_ = nowMs - kThemeFrameMs;
}

void UiController::setThemeResting(const bool resting, const std::uint32_t nowMs) {
    const std::uint8_t target = resting ? 255 : 0;
    if (restTarget_ == target) {
        return;
    }
    restFrom_ = sampledRestAmount(nowMs);
    restTarget_ = target;
    restTransitionStartedAtMs_ = nowMs;
    lastThemeFrameMs_ = nowMs - kThemeFrameMs;
}

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

const char* UiController::activeArtworkId() const noexcept { return artworkId_; }

const char* UiController::stagedArtworkId() const noexcept {
    return hasStagedArtwork_ ? stagedArtwork_.artworkId : "";
}

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

bool UiController::sameTrack(const app::PlaybackSnapshot& left,
                             const app::PlaybackSnapshot& right) noexcept {
    if (left.trackId[0] != '\0' && right.trackId[0] != '\0') {
        return std::strcmp(left.trackId, right.trackId) == 0;
    }
    return std::strcmp(left.title, right.title) == 0 &&
           std::strcmp(left.artist, right.artist) == 0 && std::strcmp(left.album, right.album) == 0;
}

bool UiController::idlePlayback(const app::PlaybackSnapshot& snapshot) noexcept {
    return snapshot.title[0] == '\0' &&
           (snapshot.playerId[0] == '\0' || snapshot.status == app::PlaybackStatus::Stopped);
}

bool UiController::trackChanged(const app::PlaybackSnapshot& snapshot) const noexcept {
    return hasPlayback_ && !sameTrack(playback_, snapshot);
}

bool UiController::artworkMatches(const app::PlaybackSnapshot& snapshot,
                                  const app::ArtworkResult& result) noexcept {
    return app::artworkIdentityMatches(snapshot.artworkId, snapshot.artworkGeneration,
                                       result.artworkId, result.artworkGeneration) &&
           snapshot.hasTheme == result.hasTheme &&
           (!snapshot.hasTheme || core::themesEqual(snapshot.theme, result.theme));
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
    if (hasPendingPlayback_ && sameTrack(pendingPlayback_, snapshot)) {
        pendingPlayback_ = snapshot;
        if (hasStagedArtwork_ && !artworkMatches(pendingPlayback_, stagedArtwork_)) {
            hasStagedArtwork_ = false;
        }
        if (transitionSwapped_) {
            const bool fallbackChanged =
                snapshot.artworkGeneration != 0 && snapshot.hasTheme &&
                snapshot.artworkId[0] == '\0' &&
                (artworkId_[0] != '\0' || artworkGeneration_ != snapshot.artworkGeneration ||
                 !core::themesEqual(themeTarget_, snapshot.theme));
            applyPlayback(snapshot, nowMs);
            bool coverChanged = false;
            if (hasStagedArtwork_ && artworkMatches(playback_, stagedArtwork_)) {
                coverChanged = commitArtwork(stagedArtwork_, nowMs);
                hasStagedArtwork_ = false;
            }
            if ((fallbackChanged || coverChanged) && screen_ == Screen::NowPlaying &&
                !connectionScreenActive() && volumeOverlayUntilMs_ == 0 && toastUntilMs_ == 0 &&
                !factoryResetChordVisible_ && !bootRendered_) {
                renderArtwork(nowMs);
            }
            if (screen_ == Screen::NowPlaying && volumeOverlayUntilMs_ == 0 && toastUntilMs_ == 0) {
                renderFooter(nowMs);
            }
        }
        return;
    }
    const bool currentIdle = !hasPlayback_ || idlePlayback(playback_);
    const bool nextIdle = idlePlayback(snapshot);
    if (hasPlayback_ && currentIdle != nextIdle) {
        applyPlayback(snapshot, nowMs);
        hasPendingPlayback_ = false;
        hasStagedArtwork_ = false;
        transitionSwapped_ = false;
        dirty_ = true;
        return;
    }
    if (trackChanged(snapshot) && screen_ == Screen::NowPlaying && !connectionScreenActive()) {
        pendingPlayback_ = snapshot;
        hasPendingPlayback_ = true;
        hasStagedArtwork_ = false;
        transitionSwapped_ = false;
        transitionStartedAtMs_ = nowMs;
        return;
    }
    const bool metadataChanged =
        !hasPlayback_ || std::strcmp(playback_.title, snapshot.title) != 0 ||
        std::strcmp(playback_.artist, snapshot.artist) != 0 || queueChanged(snapshot);
    const bool footerChanged = !hasPlayback_ || playback_.status != snapshot.status ||
                               playback_.shuffleKnown != snapshot.shuffleKnown ||
                               playback_.shuffle != snapshot.shuffle ||
                               playback_.hasDuration != snapshot.hasDuration ||
                               playback_.durationMs != snapshot.durationMs;
    const bool actionsChanged = !hasPlayback_ || playback_.shuffleKnown != snapshot.shuffleKnown ||
                                playback_.shuffle != snapshot.shuffle ||
                                playback_.repeat != snapshot.repeat;
    const bool fallbackChanged =
        snapshot.artworkGeneration != 0 && snapshot.hasTheme && snapshot.artworkId[0] == '\0' &&
        (artworkId_[0] != '\0' || artworkGeneration_ != snapshot.artworkGeneration ||
         !core::themesEqual(themeTarget_, snapshot.theme));
    applyPlayback(snapshot, nowMs);
    if (screen_ == Screen::NowPlaying && !connectionScreenActive() && volumeOverlayUntilMs_ == 0 &&
        toastUntilMs_ == 0 && !factoryResetChordVisible_ && !bootRendered_ &&
        !hasPendingPlayback_) {
        if (fallbackChanged) {
            renderArtwork(nowMs);
        }
        if (metadataChanged) {
            renderMetadata(nowMs, kText);
        }
        if (footerChanged) {
            renderFooter(nowMs);
        } else {
            renderProgress(nowMs);
        }
    } else if (bootRendered_ || fallbackChanged || (screen_ == Screen::Actions && actionsChanged)) {
        dirty_ = true;
    }
}

void UiController::applyPlayback(const app::PlaybackSnapshot& snapshot, const std::uint32_t nowMs) {
    const bool titleChanged = !hasPlayback_ || std::strcmp(playback_.title, snapshot.title) != 0;
    playback_ = snapshot;
    hasPlayback_ = true;
    if (titleChanged) {
        resetTitleScroll(nowMs);
    }
    progress_.synchronize(snapshot.positionMs, snapshot.hasDuration ? snapshot.durationMs : 0,
                          snapshot.status == app::PlaybackStatus::Playing, nowMs);
    setThemeResting(snapshot.status != app::PlaybackStatus::Playing, nowMs);
    if (snapshot.artworkGeneration != 0 && snapshot.hasTheme && snapshot.artworkId[0] == '\0') {
        commitFallback(snapshot, nowMs);
    }
}

void UiController::commitFallback(const app::PlaybackSnapshot& snapshot,
                                  const std::uint32_t nowMs) {
    if (snapshot.artworkGeneration == 0 || !snapshot.hasTheme || snapshot.artworkId[0] != '\0') {
        return;
    }
    const bool changed = artworkId_[0] != '\0' || artworkPath_[0] != '\0' ||
                         artworkGeneration_ != snapshot.artworkGeneration ||
                         !core::themesEqual(themeTarget_, snapshot.theme);
    if (!changed) {
        return;
    }
    artworkId_[0] = '\0';
    artworkPath_[0] = '\0';
    artworkGeneration_ = snapshot.artworkGeneration;
    transitionTheme(snapshot.theme, nowMs);
    DW_LOG_INFO("ui", "Committed fallback artwork generation %lu",
                static_cast<unsigned long>(artworkGeneration_));
}

bool UiController::commitArtwork(const app::ArtworkResult& result, const std::uint32_t nowMs) {
    if (!result.success || result.artworkId[0] == '\0' || result.localPath[0] == '\0') {
        return false;
    }
    if (result.artworkGeneration != 0 && !result.hasTheme) {
        DW_LOG_WARN("ui", "Rejected generation %lu artwork without a complete theme",
                    static_cast<unsigned long>(result.artworkGeneration));
        return false;
    }
    const auto& committedTheme = result.hasTheme ? result.theme : kFallbackTheme;
    const bool coverChanged = std::strcmp(artworkId_, result.artworkId) != 0 ||
                              std::strcmp(artworkPath_, result.localPath) != 0;
    const bool bundleChanged = coverChanged || artworkGeneration_ != result.artworkGeneration ||
                               !core::themesEqual(themeTarget_, committedTheme);
    if (!bundleChanged) {
        return false;
    }
    app::copyText(artworkId_, result.artworkId);
    app::copyText(artworkPath_, result.localPath);
    artworkGeneration_ = result.artworkGeneration;
    transitionTheme(committedTheme, nowMs);
    DW_LOG_INFO("ui", "Committed artwork %.12s generation %lu", artworkId_,
                static_cast<unsigned long>(artworkGeneration_));
    return coverChanged;
}

void UiController::setArtwork(const app::ArtworkResult& result, const std::uint32_t nowMs) {
    const bool belongsToCurrentPlayback = hasPlayback_ && artworkMatches(playback_, result);
    const bool belongsToPendingPlayback =
        hasPendingPlayback_ && artworkMatches(pendingPlayback_, result);
    if (!belongsToCurrentPlayback && !belongsToPendingPlayback) {
        DW_LOG_DEBUG("ui", "Ignored stale artwork %.12s generation %lu", result.artworkId,
                     static_cast<unsigned long>(result.artworkGeneration));
        return;
    }
    if (!result.success) {
        if (result.error[0] != '\0') {
            showToast(result.error, nowMs, true);
        }
        return;
    }
    if (result.artworkGeneration != 0 && !result.hasTheme) {
        DW_LOG_WARN("ui", "Ignored incomplete artwork bundle generation %lu",
                    static_cast<unsigned long>(result.artworkGeneration));
        return;
    }
    if (belongsToPendingPlayback && !belongsToCurrentPlayback) {
        stagedArtwork_ = result;
        hasStagedArtwork_ = true;
        DW_LOG_DEBUG("ui", "Staged artwork %.12s generation %lu", result.artworkId,
                     static_cast<unsigned long>(result.artworkGeneration));
        return;
    }
    const bool coverChanged = commitArtwork(result, nowMs);
    if (coverChanged && screen_ == Screen::NowPlaying && !connectionScreenActive() &&
        volumeOverlayUntilMs_ == 0 && toastUntilMs_ == 0 && !factoryResetChordVisible_ &&
        !bootRendered_) {
        renderArtwork(nowMs);
    } else if (screen_ == Screen::Actions) {
        dirty_ = true;
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

void UiController::setClock(const app::ClockSync& clock, const std::uint32_t nowMs) {
    (void)nowMs;
    clock_ = clock;
    clockRendered_ = false;
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
    setThemeResting(status != app::PlaybackStatus::Playing, nowMs);
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
        lastThemeFrameMs_ = nowMs;
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
    bool animatedUi = false;
    if ((screen_ == Screen::NowPlaying || screen_ == Screen::Actions) &&
        !connectionScreenActive() && volumeOverlayUntilMs_ == 0 && toastUntilMs_ == 0 &&
        !factoryResetChordVisible_) {
        if (themeAnimating(nowMs) &&
            static_cast<std::uint32_t>(nowMs - lastThemeFrameMs_) >= kThemeFrameMs) {
            renderThemeAccents(nowMs);
            lastThemeFrameMs_ = nowMs;
            animatedUi = true;
        }
    }
    if (screen_ == Screen::NowPlaying && !connectionScreenActive() && !hasPendingPlayback_ &&
        volumeOverlayUntilMs_ == 0 && toastUntilMs_ == 0 && !factoryResetChordVisible_) {
        if (idlePlayback(playback_) &&
            static_cast<std::uint32_t>(nowMs - lastIdleFrameMs_) >= kIdleFrameMs) {
            renderIdleAnimation(nowMs);
            lastIdleFrameMs_ = nowMs;
            animatedUi = true;
        }
        if (!idlePlayback(playback_) && titleScrollActive_ &&
            static_cast<std::uint32_t>(nowMs - lastTitleFrameMs_) >= kTitleFrameMs) {
            renderTitle(nowMs, kText);
            lastTitleFrameMs_ = nowMs;
            animatedUi = true;
        }
        if (!idlePlayback(playback_) &&
            static_cast<std::uint32_t>(nowMs - lastProgressFrameMs_) >= kProgressFrameMs) {
            renderProgress(nowMs);
            lastProgressFrameMs_ = nowMs;
            animatedUi = true;
        }
    }
    if (animatedUi) {
        return;
    }
    if (connectionScreenActive() &&
        static_cast<std::uint32_t>(nowMs - lastAnimationFrameMs_) >= 250) {
        renderConnection(nowMs);
        lastAnimationFrameMs_ = nowMs;
    }
}

void UiController::tickTrackTransition(const std::uint32_t nowMs) {
    if (screen_ != Screen::NowPlaying || connectionScreenActive()) {
        applyPlayback(pendingPlayback_, nowMs);
        if (hasStagedArtwork_ && artworkMatches(playback_, stagedArtwork_)) {
            (void)commitArtwork(stagedArtwork_, nowMs);
        }
        hasStagedArtwork_ = false;
        hasPendingPlayback_ = false;
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
            applyPlayback(pendingPlayback_, nowMs);
            if (hasStagedArtwork_ && artworkMatches(playback_, stagedArtwork_)) {
                (void)commitArtwork(stagedArtwork_, nowMs);
            }
            hasStagedArtwork_ = false;
            transitionSwapped_ = true;
            if (idlePlayback(playback_)) {
                hasPendingPlayback_ = false;
                transitionSwapped_ = false;
                dirty_ = true;
                return;
            }
            renderArtwork(nowMs);
            renderFooter(nowMs);
        }
        const auto secondElapsed = std::min<std::uint32_t>(elapsed - half, half);
        const auto amount = static_cast<std::uint8_t>(secondElapsed * 255U / half);
        renderMetadata(nowMs, blend565(kText, kBackground, amount),
                       static_cast<std::int16_t>(6 - secondElapsed * 6U / half));
    }
    lastAnimationFrameMs_ = nowMs;
    if (elapsed >= kTrackTransitionMs) {
        const bool fallbackChanged =
            pendingPlayback_.artworkGeneration != 0 && pendingPlayback_.hasTheme &&
            pendingPlayback_.artworkId[0] == '\0' &&
            (artworkId_[0] != '\0' || artworkGeneration_ != pendingPlayback_.artworkGeneration ||
             !core::themesEqual(themeTarget_, pendingPlayback_.theme));
        applyPlayback(pendingPlayback_, nowMs);
        bool coverChanged = false;
        if (hasStagedArtwork_ && artworkMatches(playback_, stagedArtwork_)) {
            coverChanged = commitArtwork(stagedArtwork_, nowMs);
        }
        if (fallbackChanged || coverChanged) {
            renderArtwork(nowMs);
        }
        hasPendingPlayback_ = false;
        transitionSwapped_ = false;
        hasStagedArtwork_ = false;
        renderMetadata(nowMs, kText);
        renderFooter(nowMs);
    }
}

void UiController::render(const std::uint32_t nowMs) {
    progressPainted_ = false;
    renderAtmosphere();
    if (screen_ != Screen::NowPlaying) {
        renderHeader(nowMs);
    }
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
            renderActions(nowMs);
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
    const bool themed = screen_ == Screen::NowPlaying && hasPlayback_ && !idlePlayback(playback_) &&
                        !connectionScreenActive();
    const auto theme = renderTheme(millis());
    const auto canvas = themed ? canvasBackground(theme) : kBackground;
    const auto lift = themed ? blend565(theme.secondary, canvas, 28) : kBackgroundLift;
    display_.fillScreen(canvas);
    if (themed) {
        // The artwork-derived canvas is intentionally broad and calm, like Spotify's
        // lyrics backdrop. Keep it opaque and RGB565-friendly rather than sprinkling
        // the reclaimed header space with the generic star field.
        display_.fillRect(0, 0, 320, kProgressY, blend565(lift, canvas, 54));
        display_.fillRect(kMetadataX - 4, 0, 320 - (kMetadataX - 4), kProgressY,
                          blend565(theme.secondary, canvas, 22));
        return;
    }
    for (std::int32_t y = 0; y < 240; y += 8) {
        const auto amount = static_cast<std::uint8_t>(28U + (y * 70U / 239U));
        display_.fillRect(0, y, 320, 8, blend565(lift, canvas, amount));
    }
    const auto primary = themed ? theme.primary : kAccent;
    const auto secondary = themed ? theme.secondary : kViolet;
    display_.fillCircle(301, 42, 72, blend565(secondary, canvas, 26));
    display_.fillCircle(18, 222, 68, blend565(primary, canvas, 18));
    display_.fillCircle(265, 236, 48, blend565(themed ? theme.background : kMagenta, canvas, 15));
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
                            blend565(index % 2 == 0 ? primary : secondary, canvas, 105));
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
    display_.fillCircle(15, 15, 6, kAccentDim);
    display_.fillCircle(15, 15, 2, kAccent);
    drawFitted("DESKWAVE", 28, 9, 78, &fonts::Font0, kText);
    drawFitted(screenName(screen_), 176, 9, 130, &fonts::Font0, kTextMuted,
               lgfx::textdatum_t::top_center);

    const auto connectionColor = status_.hostConnected ? kAccent : kWarning;
    display_.fillCircle(248, 15, 3, connectionColor);
    drawFitted(status_.hostConnected ? "LINKED" : "RETRY", 312, 10, 56, &fonts::Font0,
               connectionColor, lgfx::textdatum_t::top_right);
}

void UiController::renderConnection(const std::uint32_t nowMs) {
    display_.fillRoundRect(20, 12, 280, 205, 16, blend565(kPanel, kBackground, 230));
    display_.drawRoundRect(20, 12, 280, 205, 16, blend565(kViolet, kLine, 120));
    display_.drawCircle(160, 86, 31, blend565(kAccent, kBackground, 82));
    display_.drawCircle(160, 86, 23, blend565(kViolet, kBackground, 90));
    renderCompactLinkStatus(renderTheme(nowMs));
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
    if (!hasPlayback_ || idlePlayback(playback_)) {
        renderIdle(nowMs);
        return;
    }
    if (titleTextWidth_ == 0) {
        resetTitleScroll(nowMs);
    }
    renderArtwork(nowMs);
    renderMetadata(nowMs, kText);
    renderFooter(nowMs);
}

void UiController::renderIdle(const std::uint32_t nowMs) {
    display_.fillScreen(kBackground);
    clockRendered_ = false;
    renderIdleAnimation(nowMs);
    lastIdleFrameMs_ = nowMs;
}

void UiController::drawIdleWave(const std::uint32_t phase, const std::int32_t baseY,
                                const std::int32_t amplitude, const std::uint8_t thickness,
                                const std::uint16_t color) {
    constexpr std::int32_t kWaveWidth = 160;
    for (std::int32_t segment = -1; segment <= 2; ++segment) {
        const auto startX = segment * kWaveWidth - static_cast<std::int32_t>(phase);
        for (std::uint8_t stroke = 0; stroke < thickness; ++stroke) {
            const auto y = baseY + static_cast<std::int32_t>(stroke) - thickness / 2;
            display_.drawBezier(startX, y, startX + 40, y - amplitude, startX + 120, y + amplitude,
                                startX + kWaveWidth, y, color);
        }
    }
}

void UiController::renderIdleSpotify(const std::uint32_t nowMs) {
    const auto phase = nowMs % kIdleHaloPeriodMs;
    const auto distance = phase <= kIdleHaloPeriodMs / 2 ? phase : kIdleHaloPeriodMs - phase;
    const auto pulse = static_cast<std::uint8_t>(distance * 38U / (kIdleHaloPeriodMs / 2));
    const auto halo = blend565(kSpotify, kBackground, static_cast<std::uint8_t>(38U + pulse));
    display_.fillCircle(160, 42, 24 + pulse / 19, halo);
    display_.fillCircle(160, 42, 16, kSpotify);

    const auto glyph = kBackground;
    for (std::int32_t stroke = 0; stroke < 2; ++stroke) {
        display_.drawBezier(149, 36 + stroke, 155, 32 + stroke, 165, 33 + stroke, 172, 37 + stroke,
                            glyph);
    }
    display_.drawBezier(150, 41, 156, 38, 164, 39, 170, 42, glyph);
    display_.drawBezier(151, 46, 156, 43, 163, 44, 168, 47, glyph);
}

void UiController::renderIdleLinkStatus(const RenderTheme& theme) {
    const auto color = status_.hostConnected ? kAccent : kWarning;
    display_.fillRect(264, 4, 50, 12, kBackground);
    display_.fillCircle(270, 10, 2, color);
    drawFitted(status_.hostConnected ? "LINK" : "RETRY", 312, 5, 36, &fonts::Font0, color,
               lgfx::textdatum_t::top_right);
    (void)theme;
}

void UiController::renderIdleClock(const std::uint32_t nowMs, const bool clear) {
    core::ClockText text;
    std::uint64_t currentUnixMs = 0;
    std::uint64_t minute = 0;
    if (clock_.valid) {
        currentUnixMs = clock_.unixMs + static_cast<std::uint32_t>(nowMs - clock_.receivedAtMs);
        minute = currentUnixMs / 60'000U;
        (void)core::formatLocalClock(currentUnixMs, clock_.utcOffsetSeconds, text);
    }
    if (!clear && clockRendered_ && (!clock_.valid || renderedClockMinute_ == minute)) {
        return;
    }
    display_.fillRect(0, 80, 320, 94, kBackground);

    display_.setFont(&fonts::Font4);
    display_.setTextSize(2);
    const auto timeWidth = display_.textWidth(text.time);
    display_.setTextSize(1);
    display_.setFont(&fonts::Font2);
    const auto periodWidth = display_.textWidth(text.period);
    const auto totalWidth = timeWidth + 7 + periodWidth;
    const auto startX = (320 - totalWidth) / 2;

    display_.setFont(&fonts::Font4);
    display_.setTextSize(2);
    display_.setTextColor(kText);
    display_.setTextDatum(lgfx::textdatum_t::top_left);
    display_.drawString(text.time, startX, 84);
    display_.setTextSize(1);
    display_.setFont(&fonts::Font2);
    display_.drawString(text.period, startX + timeWidth + 7, 119);
    drawFitted(text.date, 160, 154, 220, &fonts::Font2, kTextMuted, lgfx::textdatum_t::top_center);

    renderedClockMinute_ = minute;
    clockRendered_ = true;
}

void UiController::renderIdleAnimation(const std::uint32_t nowMs) {
    const auto theme = renderTheme(nowMs);
    display_.fillRect(0, 0, 320, kIdleTopBandHeight, kBackground);
    display_.fillRect(0, kIdleBottomBandY, 320, 240 - kIdleBottomBandY, kBackground);

    drawIdleWave((nowMs / 60U) % 160U, 38, 15, 7, blend565(kSpotify, kBackground, 28));
    drawIdleWave((nowMs / 86U + 57U) % 160U, 62, 8, 4, blend565(kAccent, kBackground, 22));
    drawIdleWave((nowMs / 50U + 103U) % 160U, 207, 14, 6, blend565(kSpotify, kBackground, 24));

    struct Particle {
        std::uint16_t offset;
        std::uint8_t divisor;
        std::uint8_t y;
        std::uint8_t radius;
        bool accent;
    };
    static constexpr std::array<Particle, 8> kParticles{{
        {0, 48, 18, 1, true},
        {71, 61, 29, 2, false},
        {143, 39, 67, 1, true},
        {219, 72, 53, 1, false},
        {31, 55, 184, 2, true},
        {107, 43, 198, 1, false},
        {187, 67, 221, 2, true},
        {259, 51, 235, 1, false},
    }};
    for (const auto& particle : kParticles) {
        const auto travel = static_cast<std::uint32_t>(nowMs / particle.divisor + particle.offset);
        const auto x = 329 - static_cast<std::int32_t>(travel % 340U);
        const auto color =
            blend565(particle.accent ? kSpotify : kViolet, kBackground, particle.accent ? 62 : 38);
        display_.fillCircle(x, particle.y, particle.radius, color);
    }

    renderIdleSpotify(nowMs);
    renderIdleLinkStatus(theme);
    renderIdleClock(nowMs, false);
}

void UiController::renderArtworkGlow(const RenderTheme& theme) {
    constexpr std::array<std::uint8_t, 4> kLayerScales{{255, 172, 96, 46}};
    const auto glowBase = canvasBackground(theme);
    for (std::size_t layer = 0; layer < kLayerScales.size(); ++layer) {
        const auto layerStrength = scaledAmount(kArtworkGlowStrength, kLayerScales[layer]);
        const auto amount = scaledAmount(layerStrength, theme.glowScale);
        const auto accent = layer < 2 ? theme.primary : theme.secondary;
        const auto offset = static_cast<std::int32_t>(2 + layer);
        display_.drawRoundRect(kArtworkX - offset, kArtworkY - offset, kArtworkSize + offset * 2,
                               kArtworkSize + offset * 2, 9 + layer,
                               blend565(accent, glowBase, amount));
    }
}

void UiController::renderArtwork(const std::uint32_t nowMs) {
    const auto theme = renderTheme(nowMs);
    renderArtworkGlow(theme);
    bool rendered = false;
    if (artworkId_[0] != '\0' && artworkPath_[0] != '\0' && LittleFS.exists(artworkPath_)) {
        rendered =
            display_.drawJpgFile(LittleFS, artworkPath_, kArtworkX, kArtworkY, kArtworkSize,
                                 kArtworkSize, 0, 0, -1.0F, -1.0F, lgfx::textdatum_t::top_left);
    }
    if (!rendered) {
        const auto fallbackBackground = raisedPanelBackground(theme);
        display_.fillRect(kArtworkX, kArtworkY, kArtworkSize, kArtworkSize, fallbackBackground);
        for (std::int32_t row = 0; row < 4; ++row) {
            const auto y = kArtworkY + 45 + row * 19;
            const auto color =
                blend565(row % 2 == 0 ? theme.primary : theme.secondary, fallbackBackground,
                         static_cast<std::uint8_t>(185 - row * 25));
            display_.drawBezier(kArtworkX + 19, y, kArtworkX + 52, y - 17, kArtworkX + 84, y + 15,
                                kArtworkX + 112, y - 2, color);
            display_.drawBezier(kArtworkX + 112, y - 2, kArtworkX + 131, y - 13, kArtworkX + 143,
                                y + 9, kArtworkX + 153, y, color);
        }
        display_.fillCircle(kArtworkX + 82, kArtworkY + 96, 7, theme.foreground);
        display_.drawLine(kArtworkX + 89, kArtworkY + 95, kArtworkX + 89, kArtworkY + 58,
                          theme.foreground);
        display_.drawLine(kArtworkX + 89, kArtworkY + 58, kArtworkX + 113, kArtworkY + 52,
                          theme.foreground);
    }
}

void UiController::resetTitleScroll(const std::uint32_t nowMs) {
    const char* title = playback_.title[0] == '\0' ? "Untitled" : playback_.title;
    const bool japanese = needsJapaneseFont(title);
    display_.setFont(fontForText(title, &fonts::Font4));
    display_.setTextSize(japanese ? 2 : 1);
    titleTextWidth_ = static_cast<std::int16_t>(display_.textWidth(title));
    display_.setTextSize(1);
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
        const auto cycle = static_cast<std::uint32_t>(std::max<std::int16_t>(titleTextWidth_, 1) +
                                                      kTitleRepeatGap);
        const auto phase = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(nowMs - titleScrollStartedAtMs_) * kTitlePixelsPerSecond /
             1'000U) %
            cycle);
        scrollOffset = -static_cast<std::int32_t>(phase);
    }

    const auto metadataBackground = panelBackground(renderTheme(nowMs));
    display_.setClipRect(kTitleX, kTitleY, kTitleWidth, kTitleHeight);
    display_.fillRect(kTitleX, kTitleY, kTitleWidth, kTitleHeight, metadataBackground);
    const bool japanese = needsJapaneseFont(title);
    display_.setFont(fontForText(title, &fonts::Font4));
    display_.setTextSize(japanese ? 2 : 1);
    display_.setTextColor(color);
    display_.setTextDatum(lgfx::textdatum_t::top_left);
    const auto titleX = kTitleX + xOffset + scrollOffset;
    display_.drawString(title, titleX, kTitleY + 1);
    if (titleScrollActive_ && xOffset == 0) {
        display_.drawString(title, titleX + titleTextWidth_ + kTitleRepeatGap, kTitleY + 1);
    }
    display_.setTextSize(1);
    display_.drawFastHLine(kTitleX, kTitleY + kTitleHeight - 1, kTitleWidth,
                           blend565(renderTheme(nowMs).primary, metadataBackground, 92));
    display_.clearClipRect();
}

void UiController::renderMetadata(const std::uint32_t nowMs, const std::uint16_t color,
                                  const std::int16_t xOffset) {
    const auto theme = renderTheme(nowMs);
    const auto canvas = canvasBackground(theme);
    display_.fillRect(kMetadataX - 3, kMetadataY - 3, kMetadataWidth + 8, kMetadataHeight + 7,
                      canvas);
    const auto metadataBackground = panelBackground(theme);
    display_.fillRoundRect(kMetadataX, kMetadataY, kMetadataWidth, kMetadataHeight, 8,
                           metadataBackground);
    display_.drawRoundRect(kMetadataX, kMetadataY, kMetadataWidth, kMetadataHeight, 8,
                           blend565(theme.secondary, kBackground, 78));

    const auto x = kTitleX + xOffset;
    const auto fadedAccent = color == kText ? theme.primary : blend565(theme.primary, canvas, 105);
    const auto fadedArtist = color == kText ? kTextMuted : blend565(kTextMuted, canvas, 105);
    drawFitted("TRACK", x, kTrackLabelY, 48, &fonts::Font0, fadedAccent);
    renderTitle(nowMs, color, xOffset);
    drawFitted(playback_.artist[0] == '\0' ? "Unknown artist" : playback_.artist, x, kArtistY,
               kTitleWidth, &fonts::Font2, fadedArtist);

    const auto queueX = kMetadataX + 7;
    const auto queueWidth = kMetadataWidth - 14;
    display_.fillRoundRect(queueX, kQueueY, queueWidth, kQueueHeight, 7,
                           blend565(raisedPanelBackground(theme), metadataBackground, 155));
    display_.drawRoundRect(queueX, kQueueY, queueWidth, kQueueHeight, 7,
                           blend565(theme.secondary, metadataBackground, 88));
    drawFitted("UP NEXT", queueX + 7 + xOffset, kQueueY + 5, queueWidth - 14, &fonts::Font0,
               color == kText ? theme.secondary : blend565(theme.secondary, canvas, 100));
    if (!playback_.queueAvailable) {
        drawFitted("QUEUE UNAVAILABLE", queueX + 7 + xOffset, kQueueY + 31, queueWidth - 14,
                   &fonts::Font0, fadedArtist);
    } else if (playback_.queueCount == 0) {
        drawFitted("QUEUE EMPTY", queueX + 7 + xOffset, kQueueY + 31, queueWidth - 14,
                   &fonts::Font0, fadedArtist);
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
            drawFitted(label, queueX + 7 + xOffset, kQueueY + 23 + index * 17, queueWidth - 14,
                       &fonts::Font0, fadedArtist);
        }
    }
    renderCompactLinkStatus(theme);
}

void UiController::renderCompactLinkStatus(const RenderTheme& theme) {
    const auto background = panelBackground(theme);
    const auto color = status_.hostConnected ? theme.primary : kWarning;
    display_.fillRect(kMetadataX + kMetadataWidth - 49, kMetadataY + 3, 44, 11, background);
    display_.fillCircle(kMetadataX + kMetadataWidth - 43, kMetadataY + 8, 2, color);
    drawFitted(status_.hostConnected ? "LINK" : "RETRY", kMetadataX + kMetadataWidth - 6,
               kMetadataY + 4, 34, &fonts::Font0, color, lgfx::textdatum_t::top_right);
}

void UiController::renderThemeLabels(const RenderTheme& theme) {
    const auto metadataBackground = panelBackground(theme);
    const auto queueX = kMetadataX + 7;
    const auto queueWidth = kMetadataWidth - 14;
    const auto queueBackground = blend565(raisedPanelBackground(theme), metadataBackground, 155);
    display_.drawRoundRect(kMetadataX, kMetadataY, kMetadataWidth, kMetadataHeight, 8,
                           blend565(theme.secondary, kBackground, 78));
    display_.fillRect(kMetadataX + 8, kMetadataY + 4, 50, 10, metadataBackground);
    drawFitted("TRACK", kTitleX, kTrackLabelY, 48, &fonts::Font0, theme.primary);
    display_.drawRoundRect(queueX, kQueueY, queueWidth, kQueueHeight, 7,
                           blend565(theme.secondary, metadataBackground, 88));
    display_.fillRect(queueX + 6, kQueueY + 3, 86, 11, queueBackground);
    drawFitted("UP NEXT", queueX + 7, kQueueY + 5, 84, &fonts::Font0, theme.secondary);
    renderCompactLinkStatus(theme);
}

void UiController::renderThemeAccents(const std::uint32_t nowMs) {
    const auto theme = renderTheme(nowMs);
    if (screen_ == Screen::Actions) {
        renderActionAccents(theme, true);
        return;
    }
    if (screen_ != Screen::NowPlaying || connectionScreenActive()) {
        return;
    }
    if (idlePlayback(playback_)) {
        return;
    }
    // Keep the large visible surfaces in the same palette transition as the icons without
    // redrawing or re-decoding the JPEG. These are all bounded opaque RGB565 patches.
    renderNowPlayingBackdrop(theme);
    renderArtworkGlow(theme);
    if (!hasPendingPlayback_) {
        renderMetadata(nowMs, kText);
    }
    renderFooter(nowMs);
}

void UiController::renderNowPlayingBackdrop(const RenderTheme& theme) {
    const auto canvas = canvasBackground(theme);
    // Only repaint canvas that is not covered by artwork, metadata, progress, or controls.
    // This makes the background visibly travel with the palette without a full-screen redraw.
    display_.fillRect(0, 0, 320, std::max<std::int32_t>(0, kArtworkY - 5), canvas);
    display_.fillRect(0, kArtworkY - 5, kArtworkX - 1, kMetadataHeight + 10, canvas);
    display_.fillRect(kArtworkX + kArtworkSize + 6, kArtworkY - 5,
                      kMetadataX - (kArtworkX + kArtworkSize + 6), kMetadataHeight + 10, canvas);
    display_.fillRect(kMetadataX + kMetadataWidth + 3, kArtworkY - 5,
                      320 - (kMetadataX + kMetadataWidth + 3), kMetadataHeight + 10, canvas);
    display_.fillRect(0, kMainBottomY, 320, kProgressY - kMainBottomY, canvas);
}

void UiController::drawTransportIcon(const std::int32_t centerX, const std::int32_t centerY,
                                     const app::PlaybackStatus status, const std::uint16_t fill,
                                     const std::uint16_t outline, const std::uint16_t glyph,
                                     const std::uint8_t pulse) {
    const auto radius = 10 + pulse;
    display_.fillCircle(centerX, centerY, radius, fill);
    display_.drawCircle(centerX, centerY, radius, outline);
    if (status == app::PlaybackStatus::Playing) {
        display_.fillRect(centerX - 4, centerY - 6, 3, 12, glyph);
        display_.fillRect(centerX + 2, centerY - 6, 3, 12, glyph);
    } else {
        display_.fillTriangle(centerX - 4, centerY - 7, centerX - 4, centerY + 7, centerX + 7,
                              centerY, glyph);
    }
}

void UiController::renderFooter(const std::uint32_t nowMs) {
    const auto theme = renderTheme(nowMs);
    const auto canvas = canvasBackground(theme);
    const auto controlsBackground = panelBackground(theme);
    const auto playBackground = raisedPanelBackground(theme);
    progressPainted_ = false;
    display_.fillRect(0, kProgressY, 320, kProgressHeight,
                      blend565(controlsBackground, canvas, 145));
    display_.fillRect(0, kControlsY, 320, kControlsHeight, controlsBackground);
    display_.fillRect(118, kControlsY, 84, kControlsHeight, playBackground);
    display_.fillRect(0, kControlsY, 320, 1, blend565(theme.secondary, canvas, 92));
    constexpr std::array<std::int16_t, 4> dividers{{64, 118, 202, 256}};
    for (const auto divider : dividers) {
        display_.drawFastVLine(divider, kControlsY + 4, kControlsHeight - 8,
                               blend565(theme.secondary, controlsBackground, 92));
    }
    renderProgress(nowMs);

    renderControlIcons(nowMs, renderTheme(nowMs), false);
}

void UiController::renderControlIcons(const std::uint32_t nowMs, const RenderTheme& theme,
                                      const bool clear) {
    const auto canvas = canvasBackground(theme);
    const auto controlsBackground = panelBackground(theme);
    const auto playBackground = raisedPanelBackground(theme);
    if (clear) {
        display_.fillRect(0, kControlsY + 1, 64, kControlsHeight - 1, controlsBackground);
        display_.fillRect(65, kControlsY + 1, 53, kControlsHeight - 1, controlsBackground);
        display_.fillRect(119, kControlsY, 83, kControlsHeight, playBackground);
        display_.fillRect(203, kControlsY + 1, 53, kControlsHeight - 1, controlsBackground);
        display_.fillRect(257, kControlsY + 1, 63, kControlsHeight - 1, controlsBackground);
        display_.drawFastHLine(119, kControlsY, 83, blend565(theme.secondary, canvas, 92));
    }
    const auto shuffleEnabled = playback_.shuffleKnown && playback_.shuffle;
    const auto shuffleColor =
        shuffleEnabled ? theme.primary : blend565(theme.secondary, controlsBackground, 120);
    display_.drawLine(20, 215, 25, 215, shuffleColor);
    display_.drawLine(25, 215, 38, 227, shuffleColor);
    display_.drawLine(38, 227, 43, 227, shuffleColor);
    display_.fillTriangle(43, 223, 43, 231, 48, 227, shuffleColor);
    display_.drawLine(20, 227, 25, 227, shuffleColor);
    display_.drawLine(25, 227, 38, 215, shuffleColor);
    display_.drawLine(38, 215, 43, 215, shuffleColor);
    display_.fillTriangle(43, 211, 43, 219, 48, 215, shuffleColor);

    display_.drawFastVLine(84, 214, 16, theme.secondary);
    display_.fillTriangle(99, 213, 99, 231, 85, 222, theme.secondary);

    const std::uint8_t pulse =
        transportPulseUntilMs_ != 0 && static_cast<std::int32_t>(nowMs - transportPulseUntilMs_) < 0
            ? static_cast<std::uint8_t>((transportPulseUntilMs_ - nowMs) / 90U)
            : 0;
    drawTransportIcon(160, kTransportCenterY, playback_.status, theme.primary, canvas, canvas,
                      std::min<std::uint8_t>(pulse, 3));

    display_.drawFastVLine(236, 214, 16, theme.secondary);
    display_.fillTriangle(221, 213, 221, 231, 235, 222, theme.secondary);

    display_.fillCircle(280, kTransportCenterY, 2, theme.secondary);
    display_.fillCircle(288, kTransportCenterY, 2, theme.secondary);
    display_.fillCircle(296, kTransportCenterY, 2, theme.secondary);
}

void UiController::renderProgress(const std::uint32_t nowMs) {
    const auto theme = renderTheme(nowMs);
    const auto footerBackground = blend565(panelBackground(theme), canvasBackground(theme), 145);
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
        display_.fillRect(0, kProgressY, 320, kProgressHeight, footerBackground);
    }
    if (!progressPainted_ || progressWidth != renderedProgressWidth_) {
        renderProgressBar(nowMs, renderTheme(nowMs));
    }
    if (positionChanged) {
        display_.fillRect(0, 197, 70, 8, footerBackground);
        drawFitted(position, 8, 197, 60, &fonts::Font0, kTextMuted);
    }
    if (durationChanged) {
        display_.fillRect(246, 197, 74, 8, footerBackground);
        drawFitted(duration, 312, 197, 60, &fonts::Font0, kTextMuted, lgfx::textdatum_t::top_right);
    }
    app::copyText(renderedPosition_, position);
    app::copyText(renderedDuration_, duration);
    progressPainted_ = true;
}

void UiController::renderProgressBar(const std::uint32_t nowMs, const RenderTheme& theme) {
    const auto footerBackground = blend565(panelBackground(theme), canvasBackground(theme), 145);
    const auto progressWidth = static_cast<std::int32_t>(progress_.fraction(nowMs) * 304.0F);
    display_.fillRect(4, 185, 312, 9, footerBackground);
    display_.fillRoundRect(8, 188, 304, 3, 1, blend565(theme.background, footerBackground, 115));
    if (progressWidth > 0) {
        display_.fillRoundRect(8, 188, progressWidth, 3, 1, theme.primary);
        display_.fillCircle(8 + progressWidth, 189, 3, theme.foreground);
    }
    renderedProgressWidth_ = progressWidth;
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

void UiController::renderActions(const std::uint32_t nowMs) {
    drawFitted("Shape the sound", 18, 39, 280, &fonts::Font4, kText);
    drawFitted("Tap a luminous card", 19, 67, 260, &fonts::Font0, kTextMuted);
    display_.fillRoundRect(12, 82, 142, 98, 14, blend565(kPanel, kBackground, 235));
    display_.fillRoundRect(166, 82, 142, 98, 14, blend565(kPanel, kBackground, 235));
    drawFitted("SHUFFLE", 83, 93, 125, &fonts::Font0, kTextMuted, lgfx::textdatum_t::top_center);
    drawFitted("SMART*", 83, 157, 90, &fonts::Font0, kTextMuted, lgfx::textdatum_t::top_center);
    drawFitted("REPEAT", 237, 93, 125, &fonts::Font0, kTextMuted, lgfx::textdatum_t::top_center);
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
    renderActionAccents(renderTheme(nowMs), false);
}

void UiController::renderActionAccents(const RenderTheme& theme, const bool clear) {
    const auto cardBackground = blend565(kPanel, kBackground, 235);
    const auto footerBackground = blend565(kPanel, kBackground, 225);
    if (clear) {
        display_.fillRect(25, 112, 116, 39, cardBackground);
        display_.fillRect(179, 112, 116, 39, cardBackground);
        display_.fillRect(270, 201, 39, 39, footerBackground);
    }
    display_.drawRoundRect(12, 82, 142, 98, 14,
                           playback_.shuffleKnown && playback_.shuffle
                               ? theme.primary
                               : blend565(theme.secondary, kLine, 76));
    display_.drawRoundRect(166, 82, 142, 98, 14,
                           playback_.repeat == app::RepeatMode::Off
                               ? blend565(theme.secondary, kLine, 76)
                               : theme.secondary);
    drawFitted(playback_.shuffleKnown ? (playback_.shuffle ? "ON" : "OFF") : "N/A", 83, 119, 125,
               &fonts::Font4,
               playback_.shuffleKnown && playback_.shuffle ? theme.primary : theme.foreground,
               lgfx::textdatum_t::top_center);
    drawFitted(repeatName(playback_.repeat), 237, 119, 125, &fonts::Font2,
               playback_.repeat == app::RepeatMode::Unknown ? kTextMuted : theme.secondary,
               lgfx::textdatum_t::top_center);
    display_.fillCircle(280, 210, 2, theme.secondary);
    display_.fillCircle(288, 210, 2, theme.secondary);
    display_.fillCircle(296, 210, 2, theme.secondary);
    drawFitted("CLOSE", 288, 227, 58, &fonts::Font0, theme.secondary,
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
    const auto theme = renderTheme(nowMs);
    std::uint8_t opacity = 255;
    if (volumeOverlayUntilMs_ > nowMs && volumeOverlayUntilMs_ - nowMs < 300) {
        opacity = static_cast<std::uint8_t>((volumeOverlayUntilMs_ - nowMs) * 255U / 300U);
    }
    const auto panelColor =
        blend565(kPanelRaised, kBackground, std::max<std::uint8_t>(opacity, 80));
    display_.fillRoundRect(76, 65, 168, 108, 13, panelColor);
    display_.drawRoundRect(
        76, 65, 168, 108, 13,
        blend565(volumeOverlayMuted_ ? theme.secondary : theme.primary, panelColor, opacity));
    drawFitted(
        volumeOverlayMuted_ ? "MUTED" : "VOLUME", 160, 79, 140, &fonts::Font2,
        blend565(volumeOverlayMuted_ ? theme.secondary : theme.foreground, panelColor, opacity),
        lgfx::textdatum_t::top_center);
    display_.fillRoundRect(94, 112, 132, 10, 4, blend565(theme.background, kLine, 95));
    const auto width = static_cast<std::int32_t>(volumeOverlayPercent_ * 132 / 100);
    if (width > 0 && !volumeOverlayMuted_) {
        display_.fillRoundRect(94, 112, width, 10, 4, blend565(theme.primary, panelColor, opacity));
    }
    char percent[16];
    std::snprintf(percent, sizeof(percent), "%d%%", volumeOverlayPercent_);
    drawFitted(percent, 160, 135, 110, &fonts::Font4,
               blend565(theme.foreground, panelColor, opacity), lgfx::textdatum_t::top_center);
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
    // A queue label can contain two maximum-sized metadata fields plus its
    // prefix. Keep the whole UTF-8 source until width fitting removes complete
    // code points below.
    char buffer[300];
    app::copyText(buffer, text == nullptr ? "" : text);
    display_.setFont(fontForText(buffer, font));
    display_.setTextSize(1);
    display_.setTextColor(color);
    display_.setTextDatum(datum);
    auto length = std::strlen(buffer);
    if (display_.textWidth(buffer) > maxWidth) {
        constexpr char kEllipsis[] = "...";
        const auto ellipsisWidth = display_.textWidth(kEllipsis);
        while (length > 0 && display_.textWidth(buffer) + ellipsisWidth > maxWidth) {
            do {
                --length;
            } while (length > 0 && (static_cast<unsigned char>(buffer[length]) & 0xC0U) == 0x80U);
            buffer[length] = '\0';
        }
        if (ellipsisWidth <= maxWidth && length + sizeof(kEllipsis) <= sizeof(buffer)) {
            std::memcpy(buffer + length, kEllipsis, sizeof(kEllipsis));
        }
    }
    display_.drawString(buffer, x, y);
}

}  // namespace deskwave::ui
