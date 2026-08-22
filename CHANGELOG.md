# Changelog

All notable DeskWave changes are documented here. The project follows semantic
versioning.

## [Unreleased]

### Added

- ESP32-D0WD-V3 Revision 3.1 profile with the verified ILI9341/XPT2046 wiring,
  direct touch transport, shuffle/More targets, and private first-boot Wi-Fi
  bootstrap support.
- Immersive 320x240 Now Playing interface with framed album art, a fully readable
  side-to-side title, artist, honest queue availability, elapsed and total time,
  five-band transport controls, player identity, and cached-state presentation
  during host rediscovery.

### Fixed

- Host discovery retries no longer produce a repeating offline toast.
- In-place WebSocket recovery now restores the connected state, so playback,
  skip, shuffle, and repeat controls remain available after a brief host outage.
- Spotify position updates redraw only the progress region instead of the full
  screen, eliminating the recurring TFT flicker.
- Repeated cached artwork results no longer decode and repaint an unchanged
  album cover, and ESP32-D0WD-V3 touch targets wait for stable coordinates before
  a tap
  is classified.
- ESP32-D0WD-V3 footer hitboxes now match their visible controls exactly; informational
  header, artwork, and queue areas no longer trigger playback actions.
- The hardened user service permits the read-only netlink access required for
  Zeroconf to inspect interfaces and publish the IPv4 mDNS service.
- Ubuntu Snap players such as Spotify can now expose MPRIS metadata and controls
  to the user service; incompatible mount-namespace directives no longer place
  DeskWave under the cross-profile-blocked `unprivileged_userns` AppArmor label.

### Changed

- The ESP32-D0WD-V3 board's physical RGB status LED now flows through a fluid
  rainbow;
  the display itself uses a softer sea-glass, lavender, and moonlit palette.

## [0.1.0] - 2026-08-21

### Added

- Linux DeskWave Host with MPRIS/D-Bus metadata, deterministic player selection,
  complete playback commands, CLI diagnostics/device administration, mDNS, and
  a hardened `systemd --user` service.
- Versioned authenticated HTTP/WebSocket protocol with explicit pairing,
  per-device revocation, strict validation, command results/idempotency, health,
  player listing, and reconnect support.
- SSRF-aware host artwork pipeline with bounded decoding, 320×320 high-quality JPEG
  normalization, content-addressed atomic cache, and cache eviction.
- ESP32-D0WD-V3 firmware with centralized GPIO/profile configuration, temporary
  AP Wi-Fi provisioning, NVS schema/migration, mDNS host discovery, token
  pairing, authenticated WebSocket recovery, and bounded LittleFS artwork cache.
- Dedicated debounced input task, remappable control mapping, explicit
  application state machine, smooth progress clock, queue-isolated concurrency,
  health logging, idle dimming, and deliberate factory reset.
- Polished 320×240 UI: splash, Now Playing, Device/player selection, Actions,
  Settings, About, idle/offline/pairing/error states, metadata transitions,
  progress, transport/volume feedback, and artwork placeholder.
- Portable firmware tests, host unit/integration tests, strict Ruff/mypy checks,
  reproducible PlatformIO builds, GitHub Actions, isolated host installer, and
  complete architecture/protocol/wiring/troubleshooting/hardware-test docs.

### Security

- Tokens and pairing codes are never hardcoded or logged; protected routes
  require bearer authentication.
- Pairing is time-limited and rate-limited, stored paired tokens are hashed, and
  artwork network/file input is constrained.

### Known limitations

- Physical ESP32/display/control qualification is pending a completed hardware
  smoke-test record.
- Linux MPRIS is the sole production backend; queue display depends on the
  active player's standard TrackList support.
- Firmware updates use the wired PlatformIO path; signed OTA and hardware trust
  features are not enabled in the reference build.

[0.1.0]: https://github.com/snowpyn/deskwave/releases/tag/v0.1.0
