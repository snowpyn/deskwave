# DeskWave

DeskWave is a dedicated ESP32-D0WD-V3 desktop music controller for Linux. A small
user-level host service reads real media state through MPRIS/D-Bus, prepares
album artwork, and streams authenticated updates to the controller over the
local network.

The `0.1.0` software is implemented for the reference hardware and covered by
clean firmware builds, portable firmware tests, host unit/integration tests,
formatting, lint, and strict type checking. Physical assembly and hardware
behavior must still be verified on the target device with the documented
[smoke test](docs/HARDWARE_SMOKE_TEST.md); no physical result is inferred from
the automated checks.

## What it does

- Shows actual title, artist, album, artwork, player, volume, playback state,
  and smoothly extrapolated progress.
- Derives a restrained accent palette from each validated cover on the Linux
  host, then uses it for a borderless artwork glow, control tints, and progress
  accents on the ESP32.
- Controls play/pause, previous/next, volume, mute, seeking, shuffle, repeat,
  and manual MPRIS player selection where the active application supports it.
- Works with Spotify, VLC, browsers, and other Linux applications that expose
  the standard MPRIS interface.
- Provisions Wi-Fi through a password-protected temporary access point; no
  firmware rebuild is needed to change networks.
- Discovers the host with mDNS and pairs with a one-time six-digit code. All
  state, control, player-list, and artwork routes require the resulting random
  bearer token.
- Recovers from Wi-Fi loss, host restart, desktop suspend/wake, player exit,
  malformed messages, and artwork failures without blocking physical input.
- Keeps the last valid cover and its matching palette through artwork loading,
  brief MPRIS metadata gaps, pause, and short reconnects. A deliberate branded
  fallback replaces them only when the active track is confirmed to have no
  usable artwork.
- Provides Now Playing, Device, Settings, Actions, About, provisioning, pairing,
  reconnecting, and error screens. With no active media, the idle state becomes
  an animated Spotify clock using the host's local time and date. Now Playing
  shows the next two tracks when the active MPRIS player exposes its standard TrackList.
- Acts only as a remote control and display. Audio continues playing on the
  selected PC or phone; DeskWave never receives or outputs the audio stream.

## System overview

```text
MPRIS player -> session D-Bus -> DeskWave Host -> HTTP/WebSocket over LAN
                                                        |
                                                        v
                                  ESP32-D0WD-V3 -> ILI9341 display + touch controls
```

The host and device communicate only on the LAN. The service does not require
Spotify developer credentials or a cloud account. See
[Architecture](docs/ARCHITECTURE.md) and [Protocol](docs/PROTOCOL.md) for the
component and wire-level design.

## Interface reference

The following is a documentation layout diagram, not a photograph, generated
render, or claim of physical-device verification:

```text
+------------+--------------------------------+
|            | TRACK TITLE                    |
|  ARTWORK   | Artist                         |
|            | Album                          |
|            |                                |
|            | 01:42  ========-----  03:58    |
+------------+--------------------------------+
| Host connected     PREV   PLAY   NEXT   72% |
+---------------------------------------------+
```

The implemented 320×240 interface also includes responsive volume and command
feedback overlays, device/player selection, settings, actions, about, idle,
provisioning, pairing, reconnecting, and explicit error states. Its actual
appearance and display orientation remain part of the physical smoke test.

## Reference hardware

- ESP32-D0WD-V3 Revision 3.1 board (classic ESP32 module)
- 240×320 SPI ILI9341 display used in 320×240 landscape orientation
- XPT2046-compatible resistive touch overlay
- Common-anode RGB status LED with suitable current limiting
- Stable 3.3 V logic, appropriate display power, and a shared ground

All board-specific assumptions are centralized in
[`firmware/include/config/hardware_config.h`](firmware/include/config/hardware_config.h).
The exact reference pinout and electrical cautions are in
[Wiring](docs/WIRING.md). Confirm the wiring before applying power.

### ESP32-D0WD-V3 board

The published firmware target is `esp32-d0wd-v3`, backed by PlatformIO's
`esp32dev` definition for the locally verified ESP32-D0WD-V3 Revision 3.1 board.
It uses the known-good ILI9341/XPT2046 wiring and touch controls: tap the footer
controls, swipe vertically to turn the encoder, and swipe horizontally for
previous/next. Build and flash it with:

```bash
.tools/bin/pio run -e esp32-d0wd-v3
.tools/bin/pio run -e esp32-d0wd-v3 -t upload --upload-port /dev/ttyUSB1
```

On the headerless Now Playing screen, tap the footer controls directly. Tap the
tiny top-right `LINK`/`RETRY` target to open the full action palette. A phone is controllable when
it is exposed to the Linux desktop as an MPRIS player, such as through KDE
Connect; otherwise the host has no phone media session to command.

## Quick start

### 1. Clone

```bash
git clone https://github.com/snowpyn/deskwave.git
cd deskwave
```

### 2. Install DeskWave Host

On a Linux desktop with Python 3.11 or newer:

```bash
./host/scripts/install.sh --enable
~/.local/bin/deskwave-host doctor
```

The installer creates an isolated virtual environment at
`~/.local/share/deskwave/venv`, installs the CLI symlink at
`~/.local/bin/deskwave-host`, copies a user configuration if one is absent,
and enables the hardened `systemd --user` service. It never requires root.

For a foreground development run instead:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -e 'host[dev]'
.venv/bin/deskwave-host run
```

The service listens on TCP port `8765` and advertises
`_deskwave._tcp.local.` over mDNS. If a firewall is active, permit trusted-LAN
access to TCP 8765 and mDNS UDP 5353. Do not expose the service to the public
internet.

### 3. Build and flash the ESP32-D0WD-V3 firmware

```bash
python3 -m venv .tools
.tools/bin/python -m pip install platformio==6.1.19
.tools/bin/pio run -e esp32-d0wd-v3
.tools/bin/pio device list
.tools/bin/pio run -e esp32-d0wd-v3 -t upload --upload-port /dev/ttyUSB1
```

Use the port reported for the connected ESP32-D0WD-V3 board; the Revision 3.1
board is expected on `/dev/ttyUSB1` in the verified workstation setup. The
initial release deliberately uses a recoverable wired update path; unsigned OTA
updates are not implemented.

### 4. Provision Wi-Fi

On first boot, the screen shows a unique `DeskWave-xxxx` access-point name and
random 12-character password.

1. Connect a phone or laptop to that access point.
2. Open `http://192.168.4.1` if the setup page does not appear automatically.
3. Enter the destination Wi-Fi network and submit.

The temporary AP stops after the credentials are committed. DeskWave retries
failed connections with bounded exponential backoff rather than rebooting.

### 5. Pair

Once DeskWave discovers the host, it displays a six-digit code. Approve that
specific pending request on the desktop:

```bash
deskwave-host pair
deskwave-host pair 123456
```

Use the code shown on your device. After the device collects its approval, the
paired record keeps only a SHA-256 hash of the issued token; the device stores
the token in its NVS settings namespace. Pending plaintext approval tokens are
deleted on collection or five-minute expiry.

### 6. Play media

Launch an MPRIS-compatible application and start playback. DeskWave selects an
actively playing application deterministically. On the Device screen, turn the
encoder and press it to choose another detected player manually.

## Touch controls

| Touch gesture | Action |
| --- | --- | --- |
| Tap Shuffle | Toggle shuffle | — |
| Tap Previous / Next | Previous / next track | Hold to seek backward / forward |
| Tap center Play | Play/pause | Hold to mute/unmute |
| Tap More | Open/close Actions | — |
| Vertical swipe | Volume ± configured step | — |
| Horizontal swipe | Previous / next track | — |

On the Actions screen, tap Shuffle or Repeat to change them. Unsupported
MPRIS capabilities are shown as unavailable and are never fabricated.

On the Settings screen:

- Swipe vertically to select a row.
- Tap a row to change brightness, idle dim timeout, volume step, or default startup
  screen.
- Select Factory reset, then hold the center Play area to confirm.

## Host CLI

```text
deskwave-host run                 Run in the foreground
deskwave-host status              Query the local health endpoint
deskwave-host devices             List paired devices and last-seen times
deskwave-host pair [CODE]         List or approve a pending device
deskwave-host revoke DEVICE_ID    Revoke one device token
deskwave-host doctor              Check config, binding, mDNS, D-Bus, and MPRIS
deskwave-host version             Show host and protocol versions
```

Runtime locations follow the Linux XDG conventions:

| Purpose | Default path |
| --- | --- |
| Configuration | `~/.config/deskwave/config.toml` |
| Processed artwork and palette cache | `~/.cache/deskwave/artwork/` |
| Pairing state | `~/.local/state/deskwave/devices.sqlite3` |

See [`host/config.example.toml`](host/config.example.toml) and
[`host/README.md`](host/README.md) for host options and service operations.

## Configuration

Host settings live in `~/.config/deskwave/config.toml`; the installer creates a
documented starter file without overwriting an existing one. Bind address,
port, log level, preferred player, artwork size limits, and private-artwork-host
policy can be changed there. Environment overrides are listed in
[`host/config.example.toml`](host/config.example.toml).

Artwork downloading, validation, SHA-256 hashing, 320x320 normalization, palette
extraction, and content-addressed caching happen on the Linux host. The device
receives only the normalized JPEG and four packed theme colors; it never
extracts a palette or decodes JPEG data on animation frames.

Playback metadata remains UTF-8 end to end. The firmware automatically selects
its bundled proportional Japanese font for non-ASCII titles, artists, and queue
entries; long strings are shortened only at UTF-8 character boundaries, and
long titles use the normal continuous marquee.

Device settings are changed on the Settings screen and stored in versioned NVS.
They include brightness, idle dim timeout, default screen, and volume step. The
first-boot provisioning page also accepts an optional host address and port for
networks where mDNS is not available; leave the address blank to use automatic
discovery. Ordinary users do not need to edit firmware source for Wi-Fi, pairing,
display preferences, or player selection.

For a dedicated device that must join one known network on first boot, copy
`firmware/include/config/device_secrets.example.h` to `device_secrets.h` and
fill in the private values. The real file is ignored by Git; the firmware saves
the profile to NVS. Ordinary builds retain the provisioning portal.

Smart Shuffle is intentionally not fabricated. MPRIS and Spotify's supported
playback-control API expose shuffle as on/off only, so the Actions screen marks
Smart Shuffle unavailable while normal shuffle and repeat remain live controls.

## Development and verification

Firmware:

```bash
.tools/bin/pio run -e esp32-d0wd-v3 -t clean
.tools/bin/pio run -e esp32-d0wd-v3
.tools/bin/pio test -e native
```

Host:

```bash
.venv/bin/ruff format --check host scripts
.venv/bin/ruff check host scripts
.venv/bin/mypy host/src
.venv/bin/pytest -q host/tests --ignore=host/tests/integration
.venv/bin/pytest -q host/tests/integration
```

Version and C++ formatting:

```bash
.venv/bin/python scripts/check_version.py
find firmware -type f \( -name '*.cpp' -o -name '*.h' \) -print0 \
  | xargs -0 .venv/bin/clang-format --dry-run --Werror
```

GitHub Actions runs the same build, format, lint, type, unit, integration, and
wheel checks on pull requests and pushes to `main`. The dedicated
[hardware smoke test](docs/HARDWARE_SMOKE_TEST.md) remains a physical gate and
cannot be replaced by CI.

## Troubleshooting

Start with `deskwave-host doctor`, the user-service journal, and the status shown
on the device. The complete [troubleshooting guide](docs/TROUBLESHOOTING.md)
covers display bring-up, controls, provisioning, discovery/firewalls, pairing,
MPRIS detection, metadata, artwork, command rejection, service startup,
suspend/wake recovery, and factory reset.

## Security model

DeskWave is designed for a trusted private LAN. Pairing, WebSocket sessions,
artwork, and player listing are authenticated, inputs and response sizes are
bounded, pairing is rate-limited, and private-network artwork fetches are
blocked by default to reduce SSRF risk. Transport is HTTP/WebSocket rather than
TLS, so network confidentiality depends on the trusted LAN. See
[SECURITY.md](SECURITY.md) before deployment.

## Current limitations

- Linux/MPRIS is the only production host backend in `0.1.0`; Windows and macOS
  can be added behind the existing backend abstraction.
- Queue display depends on the active player's standard MPRIS TrackList support;
  players without it show an explicit unavailable state.
- The published firmware targets the locally verified ILI9341/XPT2046
  ESP32-D0WD-V3 Revision 3.1 wiring profile; other boards and displays require a
  hardware adapter in the centralized config layer.
- Firmware updates are wired through PlatformIO. Safe signed OTA is reserved for
  a later release.
- Automated software verification does not prove display orientation, electrical
  integrity, encoder direction, RF performance, or end-to-end latency on a
  physical assembly. Those items are explicitly pending until the smoke-test
  record is completed.

## Roadmap

The first release intentionally keeps risky or provider-specific expansion out
of the production path. Candidate follow-up work includes signed and
rollback-safe OTA, additional centralized hardware profiles, native Windows and
macOS host backends, optional provider plugins, and measured UI/control
performance data from qualified hardware. Future queue work can expand the
compact Now Playing view for backends that expose richer queue data; the current
MPRIS TrackList path already provides the first two entries when available.

## Repository layout

```text
.github/workflows/  Firmware and host CI
docs/               Architecture, protocol, wiring, recovery, and test guides
firmware/           ESP32 application profiles and portable core tests
host/               Linux package, systemd unit, installer, and tests
scripts/            Version/build support
platformio.ini      Reproducible firmware environments
VERSION             Authoritative product version
```

## License

DeskWave is licensed under the [MIT License](LICENSE). Contributions should
follow [CONTRIBUTING.md](CONTRIBUTING.md).
