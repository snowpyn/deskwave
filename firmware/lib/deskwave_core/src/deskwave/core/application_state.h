#pragma once

#include <cstdint>

namespace deskwave::core {

enum class SystemState : std::uint8_t {
    Boot,
    Provisioning,
    ConnectingWifi,
    DiscoveringHost,
    Pairing,
    ConnectingHost,
    Ready,
    Playing,
    Paused,
    Offline,
    Error,
};

enum class StateEvent : std::uint8_t {
    BootWithCredentials,
    BootWithoutCredentials,
    CredentialsSaved,
    WifiConnected,
    WifiUnavailable,
    WifiLost,
    Retry,
    HostDiscovered,
    PairingNeeded,
    PairingComplete,
    HostConnected,
    HostDisconnected,
    PlaybackStarted,
    PlaybackPaused,
    PlaybackStopped,
    FatalError,
    Reset,
};

class StateMachine {
   public:
    [[nodiscard]] SystemState state() const noexcept;
    [[nodiscard]] bool transition(StateEvent event) noexcept;
    [[nodiscard]] bool hostConnected() const noexcept;

   private:
    SystemState state_{SystemState::Boot};
};

}  // namespace deskwave::core
