#include "deskwave/core/application_state.h"

namespace deskwave::core {

SystemState StateMachine::state() const noexcept { return state_; }

bool StateMachine::hostConnected() const noexcept {
    return state_ == SystemState::Ready || state_ == SystemState::Playing ||
           state_ == SystemState::Paused;
}

bool StateMachine::transition(const StateEvent event) noexcept {
    if (event == StateEvent::Reset) {
        state_ = SystemState::Boot;
        return true;
    }
    if (event == StateEvent::FatalError) {
        state_ = SystemState::Error;
        return true;
    }
    if (event == StateEvent::WifiLost || event == StateEvent::WifiUnavailable) {
        if (state_ != SystemState::Boot && state_ != SystemState::Provisioning &&
            state_ != SystemState::Error) {
            state_ = SystemState::Offline;
            return true;
        }
        return false;
    }

    SystemState next = state_;
    switch (state_) {
        case SystemState::Boot:
            if (event == StateEvent::BootWithCredentials) {
                next = SystemState::ConnectingWifi;
            } else if (event == StateEvent::BootWithoutCredentials) {
                next = SystemState::Provisioning;
            }
            break;
        case SystemState::Provisioning:
            if (event == StateEvent::CredentialsSaved) {
                next = SystemState::ConnectingWifi;
            }
            break;
        case SystemState::ConnectingWifi:
            if (event == StateEvent::WifiConnected) {
                next = SystemState::DiscoveringHost;
            }
            break;
        case SystemState::DiscoveringHost:
            if (event == StateEvent::HostDiscovered) {
                next = SystemState::ConnectingHost;
            } else if (event == StateEvent::PairingNeeded) {
                next = SystemState::Pairing;
            }
            break;
        case SystemState::Pairing:
            if (event == StateEvent::PairingComplete) {
                next = SystemState::ConnectingHost;
            } else if (event == StateEvent::HostDisconnected) {
                next = SystemState::DiscoveringHost;
            }
            break;
        case SystemState::ConnectingHost:
            if (event == StateEvent::HostConnected) {
                next = SystemState::Ready;
            } else if (event == StateEvent::HostDisconnected) {
                next = SystemState::DiscoveringHost;
            }
            break;
        case SystemState::Ready:
        case SystemState::Playing:
        case SystemState::Paused:
            if (event == StateEvent::PlaybackStarted) {
                next = SystemState::Playing;
            } else if (event == StateEvent::PlaybackPaused) {
                next = SystemState::Paused;
            } else if (event == StateEvent::PlaybackStopped) {
                next = SystemState::Ready;
            } else if (event == StateEvent::HostDisconnected) {
                next = SystemState::DiscoveringHost;
            }
            break;
        case SystemState::Offline:
            if (event == StateEvent::Retry) {
                next = SystemState::ConnectingWifi;
            }
            break;
        case SystemState::Error:
            break;
    }
    if (next == state_) {
        return false;
    }
    state_ = next;
    return true;
}

}  // namespace deskwave::core
