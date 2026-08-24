# DeskWave Screen and Render-State Map

## Router model

DeskWave has no URL router. `UiController::screen_` is the top-level route, `UiController::render()` is the router switch, `connectionScreenActive()` and playback fields select Now Playing substates, and three overlays compose above the selected screen.

## Top-level routes

| Logical route | Screen enum / branch | Render entry | Shared layout | How reached |
| --- | --- | --- | --- | --- |
| `/boot` | timed pre-route | `renderBoot()` | atmosphere | Automatically for 900ms after display begin |
| `/now-playing` | `Screen::NowPlaying`, usable snapshot | `renderNowPlaying()` | atmosphere + header + artwork/metadata + footer | Default screen or Menu cycle |
| `/now-playing/idle` | NowPlaying with no active player/title | `renderIdle()` | atmosphere + header | Branch inside `renderNowPlaying()` |
| `/connection` | NowPlaying with `connectionScreenActive()` | `renderConnection()` | atmosphere + header | Boot/provisioning/pairing/offline/error without retained useful playback |
| `/device` | `Screen::Device` | `renderDevice()` | atmosphere + header | Next-screen cycle |
| `/settings` | `Screen::Settings` | `renderSettings()` | atmosphere + header | Next-screen cycle |
| `/about` | `Screen::About` | `renderAbout()` | atmosphere + header | Next-screen cycle |
| `/actions` | `Screen::Actions` | `renderActions()` | atmosphere + header + action footer | More or long-Menu toggle; returns to `screenBeforeActions_` when closed |

The normal Menu cycle is `NowPlaying → Device → Settings → About → NowPlaying`. Actions is a temporary route and is not inserted into that cycle.

## Now Playing substates and transitions

| Condition/event | Visible result |
| --- | --- |
| Ready/Playing/Paused state | Active or idle Now Playing branch; connection page is suppressed |
| DiscoveringHost/ConnectingHost with a prior playback snapshot | Retain the useful Now Playing controller rather than replace it with retry UI |
| No snapshot and non-ready system state | Connection card |
| Track identity changes | Existing metadata fades/slides out, snapshot swaps at half-time, header/artwork/footer redraw, metadata fades/slides in over 220ms |
| Matching artwork result arrives | Artwork tile redraws only if the result belongs to current/pending playback |
| Position-only update | Bounded progress strip update; no route change |
| Paused | Same route; ProgressClock stops extrapolating |
| Stopped with empty title | Idle substate |

The artwork-reactive target extends the track transition with a 500–1000ms palette transition and retained resting palette, but does not add a new route.

## Overlay routes and precedence

After the base route renders, overlays compose in this exact priority:

1. Factory-reset countdown
2. Volume/mute overlay
3. Toast/error feedback

Timed volume/toast overlays suspend bounded title/progress animation until they expire. Factory-reset completion replaces the shell with a dedicated restart confirmation.

## Input navigation map

- Playback footer touch: Shuffle, Previous, Play/Pause, Next, More across the five fixed x regions.
- Playback-context top-right target: More when `x ≥ 238, y < 36` (the current context mapping includes Now Playing and About).
- Device, Settings, and Actions top-right target: Menu/next-screen at the same coordinates.
- Actions: left card toggles Shuffle, right card cycles Repeat, footer More closes.
- Device: vertical gesture changes selection; a row-area press activates the selected player.
- Settings: vertical gesture changes selection; a row-area press activates the current setting.
- Horizontal swipe maps to Previous/Next control; vertical swipe maps to encoder rotation.

## Full screen-router source

### Screen enum — `firmware/include/ui/ui_controller.h`

```cpp
enum class Screen : std::uint8_t { NowPlaying, Device, Settings, About, Actions };
```

### Navigation and control-context implementation — `firmware/src/ui/ui_controller.cpp:139`

```cpp
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
```

### Connection-subroute predicate — `firmware/src/ui/ui_controller.cpp:441`

```cpp
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

```

### Screen/overlay render switch — `firmware/src/ui/ui_controller.cpp:565`

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
```

## Full system-state route source

### `firmware/lib/deskwave_core/src/deskwave/core/application_state.h`

```cpp
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
```

### `firmware/lib/deskwave_core/src/deskwave/core/application_state.cpp`

```cpp
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
            if (event == StateEvent::HostConnected) {
                // WebSocketsClient reconnects in place after a brief host outage,
                // without returning through mDNS discovery first.
                next = SystemState::Ready;
            } else if (event == StateEvent::HostDiscovered) {
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
```
