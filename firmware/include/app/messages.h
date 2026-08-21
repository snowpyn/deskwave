#pragma once

#include <algorithm>
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
