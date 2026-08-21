# Hardware smoke test and qualification record

This procedure is the physical gate for DeskWave `0.1.0`. Automated builds and
tests cannot verify electrical safety, the exact display module, control feel,
RF conditions, suspend/wake behavior, or end-to-end latency.

**Repository status:** pending physical hardware. No item below is marked as
passed by software-only verification.

Copy this file (or attach a completed issue artifact) for each hardware revision.
Do not edit unchecked items into checked items without observing the result.

## Test record

```text
Tester:
Date/time:
DeskWave commit/tag:
Firmware binary SHA-256:
Host version:
Linux distribution/kernel:
Desktop environment:
ESP32 board/module revision:
Display module/controller marking:
Power source and measured voltage:
Wi-Fi access point/model:
MPRIS players tested:
Serial log attachment:
Host journal attachment:
```

## Safety preflight

- [ ] USB/external power was disconnected during continuity checks.
- [ ] VCC/GND resistance shows no unexplained short.
- [ ] Display supply voltage and backlight interface match its datasheet.
- [ ] Backlight is not driven directly beyond ESP32 GPIO limits.
- [ ] All modules share ground.
- [ ] Every signal matches [WIRING.md](WIRING.md); no GPIO is duplicated.
- [ ] The board is identified as an ESP32-S3-DevKitC-1-N8 or the hardware profile
      was deliberately adapted and reviewed.

Stop immediately for unexpected heating, odor, unstable supply voltage, or USB
over-current warnings.

## Reproducible build and flash

From a clean checkout:

```bash
python3 -m venv .qualification-tools
.qualification-tools/bin/python -m pip install platformio==6.1.19
.qualification-tools/bin/pio run -e esp32-s3-devkitc-1 -t clean
.qualification-tools/bin/pio run -e esp32-s3-devkitc-1
sha256sum .pio/build/esp32-s3-devkitc-1/firmware.bin
.qualification-tools/bin/pio run -e esp32-s3-devkitc-1 -t upload \
  --upload-port /dev/ttyACM0
```

- [ ] Clean build succeeds with no project warnings.
- [ ] Binary hash is recorded above.
- [ ] Upload completes and the board resets normally.
- [ ] Serial starts at 115200 and identifies the expected DeskWave version.
- [ ] Reflashing the same binary is repeatable.

## Boot and display

- [ ] Backlight starts at a safe level without a full-bright flash.
- [ ] DeskWave splash and version are legible and correctly oriented.
- [ ] Boot proceeds through product screens, not raw debug output.
- [ ] Colors are correct (red/blue not swapped), geometry is not mirrored, and
      no edge is clipped.
- [ ] No persistent tearing/flicker occurs during normal progress updates.
- [ ] Branded artwork placeholder is visually intact.
- [ ] Now Playing, Device, Settings, Actions, About, idle, offline, pairing, and
      error layouts are legible at normal viewing distance.

Record any panel flag or rotation change in the test record and hardware config.

## Encoder and buttons

- [ ] One clockwise detent produces one volume increase.
- [ ] One counter-clockwise detent produces one volume decrease.
- [ ] Encoder short press toggles play/pause once, on release.
- [ ] Encoder long press mutes/unmutes once and does not also toggle playback.
- [ ] Left/Right short presses produce exactly one previous/next command.
- [ ] Left/Right long presses seek in the expected direction only when supported.
- [ ] Continued hold produces controlled seek repeats without a command flood.
- [ ] Menu short press cycles primary screens once.
- [ ] Menu long press opens/closes Actions once.
- [ ] Rapid rotation, simultaneous network activity, and artwork decoding do not
      lose control responsiveness or fill the input queue.
- [ ] Contact bounce does not cause duplicate commands.

## Provisioning and persistence

Begin with a deliberate factory reset.

- [ ] Recovery chord requires all three buttons for five seconds and cancels on
      early release.
- [ ] Temporary `DeskWave-xxxx` AP appears with the displayed random password.
- [ ] Incorrect AP password cannot join.
- [ ] Setup page loads at `192.168.4.1` and rejects a missing/changed nonce.
- [ ] Invalid SSID/password lengths are rejected visibly.
- [ ] Valid 2.4 GHz credentials commit and the temporary AP stops.
- [ ] Device obtains IP, displays SSID/RSSI, and survives an ESP32 reboot without
      reprovisioning.
- [ ] Saved brightness, dim timeout, volume step, and default screen survive reboot.
- [ ] Factory reset clears Wi-Fi, pairing, and user settings, then returns to
      provisioning.

## Discovery and pairing

- [ ] `_deskwave._tcp.local.` is visible on the LAN.
- [ ] Device discovers the host without a hardcoded IP.
- [ ] Six-digit code appears only after host discovery.
- [ ] `deskwave-host pair` lists the correct ID/name.
- [ ] Wrong and expired codes fail without pairing another device.
- [ ] Correct approval is collected and authenticated WebSocket connects.
- [ ] Token/code/password never appears in serial or journal logs.
- [ ] ESP32 reboot reconnects without another approval.
- [ ] Revoking the device makes firmware discard the rejected token and present a
      new pairing flow without a factory reset.

## Actual playback state

For each player listed in the record:

- [ ] Title is actual and updates within the measured time below.
- [ ] Artist and album match MPRIS data or show the documented unavailable state.
- [ ] Player name is correct.
- [ ] Playing, paused, stopped, no-player, and no-music states are distinguishable
      by text/icon as well as color.
- [ ] Duration and progress are correct where provided.
- [ ] Progress moves smoothly only while playing and clamps at duration.
- [ ] A seek updates immediately, then reconciles to confirmed player position.
- [ ] Track transition does not leave stale title/artwork combinations.

## Artwork

- [ ] Local-file artwork is resized by the host, downloaded once, decoded, and shown.
- [ ] Public HTTP(S) artwork works when available.
- [ ] Repeated position resyncs retain artwork without flicker/re-download.
- [ ] Track change replaces old artwork cleanly.
- [ ] Missing artwork shows the branded placeholder.
- [ ] Malformed/non-JPEG/oversize/truncated artwork does not crash or block input.
- [ ] Host artwork timeout leaves metadata/control usable.
- [ ] ESP32 reboot repairs/uses the cache without a boot loop.

## Playback controls

- [ ] Play, pause, toggle, previous, and next execute and return confirmed state.
- [ ] Volume changes by the configured step and overlay fades after about 1–2 seconds.
- [ ] Mute restores a sensible previous volume where the player supports volume.
- [ ] Shuffle toggles only when supported.
- [ ] Repeat cycles Off → Track → Playlist → Off only when supported.
- [ ] Unsupported controls show an explicit error and do not fabricate success.
- [ ] Device screen lists real MPRIS players and manual selection changes the
      active target.
- [ ] Repeated/duplicate protocol sequence does not execute a command twice.

## Failure injection and recovery

For each step, retain serial and host logs.

### Wi-Fi

- [ ] Power off the AP: device enters Wi-Fi unavailable/offline and stays responsive.
- [ ] Leave it offline for at least five minutes: no reboot loop or reconnect storm.
- [ ] Restore the AP: device automatically reconnects and resumes confirmed state.
- [ ] Change host DHCP address: mDNS rediscovery succeeds without ESP32 reboot.

### Host and desktop

- [ ] Stop `deskwave-host`: device shows Host offline and controls fail visibly.
- [ ] Restart service: device reconnects automatically.
- [ ] Suspend desktop for at least two minutes: device remains stable.
- [ ] Wake desktop: D-Bus, service, discovery, and device recover automatically.
- [ ] Reboot desktop: device recovers without manual action.

### Player and protocol

- [ ] Exit active player: No active media player/idle state appears.
- [ ] Start a different player: deterministic selection and Device list update.
- [ ] Inject one malformed text message: session remains controlled and error is logged.
- [ ] Inject three malformed device messages: host closes that abusive session.
- [ ] Send a binary application frame: host rejects it.
- [ ] Force a command timeout: failure is returned and UI remains responsive.

### Storage and resources

- [ ] Corrupt only the disposable artwork filesystem: cache rebuilds without erasing
      Wi-Fi/pairing settings.
- [ ] Exercise the documented unsupported/corrupt settings path on a sacrificial
      device: Error is visible and reset chord remains available.
- [ ] Run continuous playback/track/volume activity for at least eight hours.
- [ ] Free heap and largest block logs show no sustained leak/fragmentation trend.
- [ ] No task watchdog reset, brownout, panic, or spontaneous reboot occurs.

## Measured performance

Use timestamps, a logic analyzer/video, or another stated method. Do not estimate
or copy target numbers into the result column.

| Measurement | Target | Method | Observed |
| --- | ---: | --- | ---: |
| Physical input → application action | <150 ms typical LAN |  |  |
| Physical input → visible feedback | <50 ms perceived where practical |  |  |
| Player track change → device state | <500 ms typical |  |  |
| Host restart → recovered state | automatic; no manual reset |  |  |
| Peak free heap during artwork/control load | record only |  |  |
| Largest free block after 8-hour soak | no sustained decline |  |  |

## Qualification decision

```text
Result: PASS / FAIL / BLOCKED
Blocking defects:
Non-blocking observations:
Hardware/profile changes committed at:
Retest required:
Approver:
```

A PASS requires every applicable checkbox, recorded measurements, and no
unresolved safety/reliability defect. A software release may explicitly carry
this gate as pending, but it must not claim physical qualification until a
completed record exists.
