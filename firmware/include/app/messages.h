#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "deskwave/core/application_state.h"
#include "deskwave/core/theme.h"

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
enum class MediaKind : std::uint8_t { Music, Podcast };
enum class RepeatMode : std::uint8_t { Unknown, Off, Track, Playlist };
enum class LyricsStatus : std::uint8_t { Unavailable, Loading, Synced, Instrumental };

struct ClockSync {
    std::uint64_t unixMs{0};
    std::uint32_t receivedAtMs{0};
    std::int32_t utcOffsetSeconds{0};
    bool valid{false};
};

inline constexpr std::size_t kMaximumLyricLines = 5;

struct LyricLine {
    std::uint64_t timeMs{0};
    char text[129]{};
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
    core::ThemePalette theme{};
    std::uint64_t durationMs{0};
    std::uint64_t positionMs{0};
    std::uint32_t receivedAtMs{0};
    std::uint32_t artworkGeneration{0};
    std::int16_t volumePercent{-1};
    PlaybackStatus status{PlaybackStatus::Stopped};
    MediaKind mediaKind{MediaKind::Music};
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
    bool hasTheme{false};
    std::array<LyricLine, kMaximumLyricLines> lyrics{};
    std::uint8_t lyricCount{0};
    LyricsStatus lyricsStatus{LyricsStatus::Unavailable};
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
    core::ThemePalette theme{};
    std::uint32_t artworkGeneration{0};
    bool hasTheme{false};
};

struct ArtworkResult {
    bool success{false};
    char artworkId[65]{};
    char localPath[96]{};
    char error[80]{};
    core::ThemePalette theme{};
    std::uint32_t artworkGeneration{0};
    bool hasTheme{false};
};

inline bool artworkIdentityMatches(const char* leftId, const std::uint32_t leftGeneration,
                                   const char* rightId,
                                   const std::uint32_t rightGeneration) noexcept {
    return leftId != nullptr && rightId != nullptr && leftId[0] != '\0' && rightId[0] != '\0' &&
           leftGeneration == rightGeneration && std::strcmp(leftId, rightId) == 0;
}

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
