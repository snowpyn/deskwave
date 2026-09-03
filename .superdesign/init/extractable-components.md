# Extractable DeskWave Components

The current renderer is monolithic: these DraftComponents correspond to complete `UiController` methods or coherent method groups that can be extracted without inventing a web component model. Props list only cross-screen state/navigation inputs. Geometry, labels, fonts, and LovyanGFX drawing commands remain hardcoded.

## Layout components

## DisplayShell
- Source: `firmware/src/ui/ui_controller.cpp` (`render`, `renderAtmosphere`)
- Category: layout
- Description: Fixed 320×240 atmosphere and top-level screen/overlay compositor.
- Extractable props: `screen` (Screen), `connectionActive` (bool), `overlay` (none/volume/toast/factory-reset)
- Hardcoded: RGB565 banded background, ambient circles/stars, canvas dimensions, overlay precedence

## HeaderBar
- Source: `firmware/src/ui/ui_controller.cpp` (`renderHeader`)
- Category: layout
- Description: Persistent 32px player/screen identity and connection status bar.
- Extractable props: `screen` (Screen), `playerName` (string), `hasPlayback` (bool), `hostConnected` (bool), `topRightAction` (more/next-screen)
- Hardcoded: Spotify/player glyph geometry, `NOW PLAYING`, `DESKWAVE`, `LINKED`, `RETRY`, all coordinates and fonts

## NowPlayingSplitLayout
- Source: `firmware/src/ui/ui_controller.cpp` (`renderNowPlaying`)
- Category: layout
- Description: Two-column artwork/metadata composition above progress and transport.
- Extractable props: `hasPlayback` (bool), `isIdle` (bool), `themeState` (fallback/playing/resting/transitioning)
- Hardcoded: 8px outer gutter, 132px artwork, 162px metadata card, 10px column gap, footer placement

## TransportFooter
- Source: `firmware/src/ui/ui_controller.cpp` (`renderFooter`, `renderProgress`)
- Category: layout
- Description: Progress band plus five exact full-height touch actions.
- Extractable props: `status` (PlaybackStatus), `shuffleKnown` (bool), `shuffleActive` (bool), `positionMs` (uint64), `durationMs` (uint64), `hasDuration` (bool), `themePalette` (runtime palette)
- Hardcoded: Shuffle/Previous/Play-Pause/Next/More order and labels, dividers, hitbox-aligned widths, geometric icons

## ActionsLayout
- Source: `firmware/src/ui/ui_controller.cpp` (`renderActions`)
- Category: layout
- Description: Two-card Shuffle/Repeat action surface with fixed close footer.
- Extractable props: `shuffleKnown` (bool), `shuffleActive` (bool), `repeatMode` (RepeatMode), `closeTarget` (bool)
- Hardcoded: card coordinates, labels, Smart Shuffle disclosure, footer copy and ellipsis geometry

## Basic components

## ArtworkTile
- Source: `firmware/src/ui/ui_controller.cpp` (`renderArtwork`)
- Category: basic
- Description: Matching LittleFS JPEG or branded waveform placeholder; target version uses a palette halo without a visible frame.
- Extractable props: `artworkId` (string), `artworkPath` (string), `artworkReady` (bool), `themePalette` (runtime palette), `themeIntensity` (0–255)
- Hardcoded: `132 × 132` size, JPEG crop/datum, placeholder waveform and music-note geometry

## MetadataCard
- Source: `firmware/src/ui/ui_controller.cpp` (`renderMetadata`)
- Category: basic
- Description: Track title, artist, and honest two-item queue preview.
- Extractable props: `title` (string), `artist` (string), `queueAvailable` (bool), `queueEntries` (up to two visible), `transitionOffset` (int16), `transitionColor` (RGB565)
- Hardcoded: `TRACK` and `UP NEXT` labels, queue empty/unavailable copy, card geometry and fonts

## ScrollingTitleStrip
- Source: `firmware/src/ui/ui_controller.cpp` (`resetTitleScroll`, `renderTitle`)
- Category: basic
- Description: Clipped single-line title with repeating horizontal marquee when wider than 144px.
- Extractable props: `title` (string), `startedAtMs` (uint32), `color` (RGB565), `transitionOffset` (int16)
- Hardcoded: 144×25 clip, 34px/s speed, 16px repeat gap, Font4, baseline divider

## QueuePreview
- Source: `firmware/src/ui/ui_controller.cpp` (`renderMetadata` queue block)
- Category: basic
- Description: Bounded first-two queue listing with truthful empty/unavailable states.
- Extractable props: `queueAvailable` (bool), `queueCount` (uint8), `entries` (QueueEntry array)
- Hardcoded: `UP NEXT`, `QUEUE UNAVAILABLE`, `QUEUE EMPTY`, numeric prefixes, two-row maximum

## ProgressBand
- Source: `firmware/src/ui/ui_controller.cpp` (`renderProgress`)
- Category: basic
- Description: Incrementally repainted 304px rail with position/duration labels.
- Extractable props: `positionMs` (uint64), `durationMs` (uint64), `hasDuration` (bool), `accentColor` (RGB565)
- Hardcoded: rail dimensions, time formatting, update patch geometry, Font0 timestamps

## TransportIcon
- Source: `firmware/src/ui/ui_controller.cpp` (`drawTransportIcon`)
- Category: basic
- Description: Dominant circular Play/Pause glyph with a short optimistic pulse.
- Extractable props: `status` (PlaybackStatus), `pulse` (uint8), `accentColor` (RGB565), `foregroundColor` (RGB565)
- Hardcoded: circle radius, pause bars, play triangle, center point when embedded in footer

## ConnectionCard
- Source: `firmware/src/ui/ui_controller.cpp` (`renderConnection`)
- Category: basic
- Description: One surface covering pairing, Wi-Fi provisioning, retry, error, and generic startup states.
- Extractable props: `systemState` (SystemState), `noticeType` (SystemNoticeType), `primary` (string), `secondary` (string), `animationPhase` (uint8)
- Hardcoded: card/ring geometry, pairing/provisioning instructions, four-dot retry animation

## IdleCard
- Source: `firmware/src/ui/ui_controller.cpp` (`renderIdle`)
- Category: basic
- Description: No-player/no-music state with musical-note mark and optional player name.
- Extractable props: `hasPlayer` (bool), `playerName` (string), `retainedPalette` (runtime palette), `themeIntensity` (0–255)
- Hardcoded: DeskWave label, no-player/no-music strings, card and note geometry

## PlayerList
- Source: `firmware/src/ui/ui_controller.cpp` (`renderDevice`)
- Category: basic
- Description: Three-row viewport into a bounded MPRIS player list.
- Extractable props: `players` (PlayerListSnapshot), `selectedPlayer` (uint8)
- Hardcoded: three visible rows, selection marker, status labels, row geometry and instruction copy

## SettingsList
- Source: `firmware/src/ui/ui_controller.cpp` (`renderSettings`)
- Category: basic
- Description: Five fixed settings rows with selected-state accent and formatted value.
- Extractable props: `settings` (SettingsView), `selectedItem` (uint8), `factoryResetConfirmation` (bool)
- Hardcoded: row names/order, five-row count, value formatters, instruction copy and geometry

## AboutHealthCard
- Source: `firmware/src/ui/ui_controller.cpp` (`renderAbout`)
- Category: basic
- Description: Firmware/protocol identity and IP, uptime, and heap health values.
- Extractable props: `version` (string), `ipAddress` (string), `wifiConnected` (bool), `uptimeSeconds` (uint32), `freeHeap` (uint32), `largestFreeBlock` (uint32)
- Hardcoded: DeskWave name, Protocol 1, open-source tagline, two-column metric layout

## VolumeOverlay
- Source: `firmware/src/ui/ui_controller.cpp` (`renderVolumeOverlay`)
- Category: basic
- Description: Timed centered volume/mute panel with fading border, rail, and percent.
- Extractable props: `percent` (int16), `muted` (bool), `expiresAtMs` (uint32), `accentColor` (RGB565)
- Hardcoded: `VOLUME`/`MUTED`, 168×108 panel, 132px rail, final 300ms fade

## Toast
- Source: `firmware/src/ui/ui_controller.cpp` (`renderToast`)
- Category: basic
- Description: Bottom command feedback panel with semantic error styling.
- Extractable props: `message` (string), `isError` (bool), `visible` (bool)
- Hardcoded: 272×40 geometry, Font2, error/accent border roles

## FactoryResetOverlay
- Source: `firmware/src/ui/ui_controller.cpp` (`renderFactoryResetOverlay`)
- Category: basic
- Description: Destructive hold confirmation with seconds remaining.
- Extractable props: `secondsRemaining` (uint8), `visible` (bool)
- Hardcoded: `FACTORY RESET`, hold instruction, warning/error colors, panel geometry

## FittedText
- Source: `firmware/src/ui/ui_controller.cpp` (`drawFitted`)
- Category: basic
- Description: Fixed-width single-line bitmap text helper with UTF-8-safe tail trimming and ellipsis.
- Extractable props: `text` (string), `maxWidth` (int32), `font` (IFont), `color` (RGB565), `datum` (textdatum)
- Hardcoded: 160-byte local buffer, three-dot ellipsis behavior, no wrapping
