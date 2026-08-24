# DeskWave Screen Dependency Trees

DeskWave has no URL pages. Each entry below is a fixed-canvas firmware screen selected by `UiController::render()`. All local includes are traced from the UI entry; system/Arduino and external library headers are omitted. The shared tree is stated once to avoid repeating the monolithic renderer for every screen.

## Shared UI dependency tree

Entry: `firmware/src/ui/ui_controller.cpp`

Dependencies:

- `firmware/include/ui/ui_controller.h`
  - `firmware/include/app/messages.h`
    - `firmware/lib/deskwave_core/src/deskwave/core/application_state.h`
  - `firmware/lib/deskwave_core/src/deskwave/core/input_logic.h`
  - `firmware/lib/deskwave_core/src/deskwave/core/progress.h`
  - `firmware/include/display/display_driver.h`
- `firmware/include/deskwave_version.h`
- `firmware/include/system/logging.h`
- `firmware/src/display/display_driver.cpp`
  - `firmware/include/display/display_driver.h`
  - `firmware/include/config/hardware_config.h`
    - `firmware/lib/deskwave_core/src/deskwave/core/pin_validation.h`

Runtime data and navigation enter through:

- `firmware/src/app/application.cpp`
  - `firmware/include/app/application.h`
    - `firmware/include/app/messages.h`
    - `firmware/include/controls/input_manager.h`
      - `firmware/lib/deskwave_core/src/deskwave/core/input_logic.h`
    - `firmware/include/network/artwork_manager.h`
    - `firmware/include/network/network_manager.h`
    - `firmware/include/storage/settings_store.h`
    - `firmware/include/ui/ui_controller.h`
- `firmware/src/controls/input_manager.cpp`
  - `firmware/include/controls/input_manager.h`
  - `firmware/include/config/hardware_config.h`
- `firmware/src/network/network_manager.cpp`
  - `firmware/include/network/network_manager.h`
  - `firmware/include/app/messages.h`
- `firmware/src/network/artwork_manager.cpp`
  - `firmware/include/network/artwork_manager.h`
  - `firmware/include/app/messages.h`

## `/boot` — Boot splash

Entry method: `UiController::renderBoot()` in `firmware/src/ui/ui_controller.cpp`

Dependencies:

- Shared UI dependency tree
- `firmware/include/deskwave_version.h` — version label
- `UiController::renderAtmosphere()` — full-canvas background
- `UiController::drawFitted()` — fixed-width bitmap text

## `/connection` — Connection, provisioning, pairing, or error state

Entry method: `UiController::renderConnection()` in `firmware/src/ui/ui_controller.cpp`

Dependencies:

- Shared UI dependency tree
- `firmware/include/app/messages.h`
  - `SystemNotice`, `SystemNoticeType`
- `firmware/lib/deskwave_core/src/deskwave/core/application_state.h`
  - `SystemState`
- `UiController::connectionScreenActive()`
- `UiController::renderHeader()`
- `UiController::drawFitted()`

The connection page occupies the Now Playing route only when there is no retained useful snapshot for Ready/Playing/Paused or host rediscovery.

## `/now-playing` — Active playback

Entry method: `UiController::renderNowPlaying()` in `firmware/src/ui/ui_controller.cpp`

Dependencies:

- Shared UI dependency tree
- `firmware/include/app/messages.h`
  - `PlaybackSnapshot`, `PlaybackStatus`, `QueueEntry`, `ArtworkResult`
- `firmware/lib/deskwave_core/src/deskwave/core/progress.h`
  - `ProgressClock`
- `firmware/src/network/artwork_manager.cpp`
  - matching JPEG download/result handoff
- `UiController::renderArtwork()`
- `UiController::renderMetadata()`
  - `UiController::renderTitle()`
  - `UiController::drawFitted()`
- `UiController::renderFooter()`
  - `UiController::renderProgress()`
  - `UiController::drawTransportIcon()`
- `UiController::tickTrackTransition()`

Host-side source relevant to the requested artwork-reactive enhancement:

- `host/src/deskwave_host/artwork.py`
  - decodes, crops, and writes the canonical artwork JPEG; this is the natural palette-extraction boundary
- `host/src/deskwave_host/models.py`
  - `PlaybackState.to_payload()` defines normalized playback fields
- `host/src/deskwave_host/api/server.py`
  - publishes state and artwork path to devices
- `host/src/deskwave_host/protocol.py`
  - protocol envelope construction and validation

## `/now-playing/idle` — No player or no active music

Entry method: `UiController::renderIdle()` in `firmware/src/ui/ui_controller.cpp`

Dependencies:

- Shared UI dependency tree
- `firmware/include/app/messages.h` — player id/name and stopped state
- `UiController::renderNowPlaying()` — branch selector
- `UiController::drawFitted()`

The target design keeps the last valid artwork palette in a softened form even when this branch renders.

## `/device` — Network and player selection

Entry method: `UiController::renderDevice()` in `firmware/src/ui/ui_controller.cpp`

Dependencies:

- Shared UI dependency tree
- `firmware/include/ui/ui_controller.h`
  - `DeviceStatus`
- `firmware/include/app/messages.h`
  - `PlayerListSnapshot`, `PlayerSummary`, `PlaybackStatus`
- `firmware/src/app/application.cpp`
  - requests/chooses players while this screen is visible
- `UiController::renderHeader()`
- `UiController::drawFitted()`

## `/settings` — Device settings

Entry method: `UiController::renderSettings()` in `firmware/src/ui/ui_controller.cpp`

Dependencies:

- Shared UI dependency tree
- `firmware/include/ui/ui_controller.h`
  - `SettingsView`
- `firmware/src/storage/settings_store.cpp`
  - persistence for brightness, default screen, idle dim, and volume step
- `firmware/src/app/application.cpp`
  - selected-row navigation, adjustment, and reset policy
- `UiController::renderHeader()`
- `UiController::drawFitted()`

## `/about` — Version and health

Entry method: `UiController::renderAbout()` in `firmware/src/ui/ui_controller.cpp`

Dependencies:

- Shared UI dependency tree
- `firmware/include/deskwave_version.h`
- `firmware/include/ui/ui_controller.h`
  - `DeviceStatus` health values
- `UiController::renderHeader()`
- `UiController::drawFitted()`

## `/actions` — Shuffle and Repeat surface

Entry method: `UiController::renderActions()` in `firmware/src/ui/ui_controller.cpp`

Dependencies:

- Shared UI dependency tree
- `firmware/include/app/messages.h`
  - shuffle availability/state and `RepeatMode`
- `firmware/lib/deskwave_core/src/deskwave/core/input_logic.cpp`
  - action-context control bindings
- `firmware/src/controls/input_manager.cpp`
  - touch card and close hit regions
- `UiController::renderHeader()`
- `UiController::drawFitted()`

## Overlay stack shared by screens

Entry selection: tail of `UiController::render()` and timed branches in `UiController::tick()`.

Dependencies:

- `UiController::renderFactoryResetOverlay()` — highest overlay priority
- `UiController::renderVolumeOverlay()` — volume/mute feedback
- `UiController::renderToast()` — command success/failure feedback
- `firmware/src/app/application.cpp` — overlay triggers and optimistic state

These are overlays rather than independently navigable pages. Their display suppresses the bounded Now Playing title/progress animation until the overlay expires or is dismissed by state change.
