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
- [ ] The board is identified as an ESP32-D0WD-V3 Revision 3.1 and the touch/display
      loom matches [WIRING.md](WIRING.md).

Stop immediately for unexpected heating, odor, unstable supply voltage, or USB
over-current warnings.

## Reproducible build and flash

From a clean checkout:

```bash
python3 -m venv .qualification-tools
.qualification-tools/bin/python -m pip install platformio==6.1.19
.qualification-tools/bin/pio run -e esp32-d0wd-v3 -t clean
.qualification-tools/bin/pio run -e esp32-d0wd-v3
sha256sum .pio/build/esp32-d0wd-v3/firmware.bin
.qualification-tools/bin/pio run -e esp32-d0wd-v3 -t upload \
  --upload-port /dev/ttyUSB1
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

## Touch input

- [ ] Display corners and the footer are visually upright after the 180-degree
      panel/touch correction.
- [ ] A tap on Shuffle, Previous, center Play, Next, and More activates the
      matching visible target exactly once.
- [ ] Holding Previous/Next seeks in the expected direction only when supported.
- [ ] Holding center Play mutes/unmutes once and does not also toggle playback.
- [ ] Vertical swipes change volume in the expected direction.
- [ ] Horizontal swipes produce exactly one previous/next command.
- [ ] Three-sample touch stabilization rejects noisy first ADC samples.
- [ ] Touches remain responsive during network activity and artwork decoding.
- [ ] Settings selection, activation, and factory-reset confirmation work through
      vertical swipe, tap, and center-Play hold gestures.

## Provisioning and persistence

Begin with a deliberate factory reset from Settings, using the center Play hold
to confirm.

- [ ] Factory-reset confirmation is required and an early release does not reset.
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
- [ ] Track transition does not leave stale title/artwork/theme combinations.

## Artwork and reactive theme

Retain host DEBUG logs, firmware logs, and video for these cases. Use a player or
controlled MPRIS fixture that can publish delayed/changed `mpris:artUrl` values,
and a test HTTP server that can return fixed, temporary-failure, and truncated
responses. Do not expose the fixture outside the trusted test LAN.

### Normal sources and visual roles

- [ ] Absolute local `file://` artwork is validated, normalized, downloaded
      once, decoded, and shown.
- [ ] Public HTTP and HTTPS artwork both work; redirects remain within the
      documented limits.
- [ ] The host-derived primary, secondary, background, and readable foreground
      colors visibly correspond to the same displayed cover.
- [ ] Now Playing has no full-width top bar, Spotify mark, player-name banner,
      or `NOW PLAYING` label; only the tiny top-right `LINK`/`RETRY` indicator
      remains, and the 166 px cover is not clipped.
- [ ] The complete exposed canvas is a deep, readable artwork-derived tone rather
      than the fixed neutral gray fallback.
- [ ] The rear RGB light visibly matches the current song-reactive canvas
      background through a track transition, scales down with idle dimming, and
      does not continue an unrelated rainbow cycle. Triggering Error still
      produces its red override.
- [ ] Red-, green-, blue-, and neutral-dominant covers exercise all three RGB
      channels; dark canvas colors remain visibly distinct instead of collapsing
      to blue-only output.
- [ ] On the verified Revision 3.1 unit, a red-dominant canvas drives GPIO 17 and
      appears physically red; GPIO 4 is the physical blue die. The generic CYD
      red-on-GPIO-4 mapping is not used for this profile.
- [ ] While active, song lighting uses the complete calibrated PWM range and
      remains at full intensity when the independent TFT brightness setting is
      changed. Idle dimming alone reduces the RGB intensity to one fifth.
- [ ] Play tracks with Japanese-only and mixed Japanese/Latin titles, artists,
      and queue entries (for example `夜に駆ける` / `YOASOBI`). Japanese glyphs
      render instead of boxes or mojibake, fitted rows end with a clean ellipsis,
      and a long Japanese title scrolls continuously without clipped UTF-8.
- [ ] Artwork has no hard frame; its restrained edge glow uses the active
      primary accent without obscuring the cover.
- [ ] Play/pause, previous, next, shuffle, repeat, mute, and progress roles use
      the active theme and remain legible in light, dark, muted, and highly
      saturated artwork cases.
- [ ] A track change interpolates glow, icons, progress, and other themed regions
      smoothly for about 750 ms, with no white/black flash or abrupt color snap.
- [ ] The old valid cover remains visible while its replacement is downloading;
      it is replaced only after the new JPEG and matching theme are ready.
- [ ] Repeated position resyncs retain the same cover/theme without flicker,
      download, JPEG decode, or palette extraction.

### Ordering, delayed metadata, and fallback

- [ ] Rapidly skip through at least ten distinct covers while requests are in
      flight; only the final track's generation can install artwork and theme.
- [ ] Delay the new track's `mpris:artUrl` beyond its first metadata snapshot;
      the prior valid cover remains through the 1.0-second grace interval, then
      the delayed cover and its own palette arrive together.
- [ ] Publish a brief empty metadata snapshot and restore the same track; cover,
      palette, and generation remain stable.
- [ ] Pause for at least 30 seconds: the same palette remains, saturation/glow
      soften gradually, and no artwork request is started merely because of
      pause.
- [ ] Resume: the same palette returns smoothly to full intensity without an
      artwork reload or snap.
- [ ] Publish a track that genuinely has no `mpris:artUrl`: after the bounded
      grace, DeskWave deliberately selects the branded placeholder and fallback
      theme rather than showing a broken or blank region.

### Fetch, validation, reconnect, and cache failures

- [ ] Return temporary HTTP 429/5xx or connection failures, then a valid image;
      logs show no more than three total attempts, with approximately 250 ms
      then 750 ms backoff, and the eventual cover.
- [ ] Keep a URL unavailable through all retries: metadata and controls stay
      usable, the previous valid cover remains until a deliberate fallback
      decision, and no retry storm occurs.
- [ ] Serve empty, non-image, oversize, corrupt, truncated, wrong-length, and
      wrong-SHA data; no incomplete object is displayed or atomically committed.
- [ ] Complete an older request after a newer track is active; logs show a stale
      generation rejection and the visible cover/theme do not change.
- [ ] Stop the host service or interrupt Wi-Fi briefly while a valid cover is
      visible; the cover and softened/current theme survive reconnection, then
      confirmed state resumes without a placeholder flash.
- [ ] Restart the host with a warm cache, block a previously cached HTTP/HTTPS
      source, and replay the same track before cache expiry; the cached JPEG and
      palette bundle are reused together without a source fetch, conversion, or
      palette extraction. Test disappeared `file://` sources separately: their
      source stat is part of the cache key, so disappearance must fail safely.
- [ ] Corrupt one host cache bundle and replay it; validation rejects the bundle
      and controlled rebuild/fallback occurs without mismatched colors.
- [ ] ESP32 reboot repairs or reuses its disposable cover cache without a boot
      loop, and SHA mismatch never replaces the last valid local file.

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
- [ ] Restart service: device reconnects automatically without clearing a valid
      cover/theme during the short outage.
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
| Artwork/theme transition duration | 500–1,000 ms; nominal 750 ms |  |  |
| Input latency during artwork/theme transition | no material regression from normal |  |  |
| Warm host artwork/palette cache reuse | no source fetch or reprocessing |  |  |
| Host restart → recovered state | automatic; no manual reset |  |  |
| Peak free heap during artwork/control load | record only |  |  |
| Largest free block after 8-hour soak | no sustained decline |  |  |

During the measured transition, verify from logs/video that JPEG decode occurs
only when a replacement cover commits, not on animation frames, and that redraws
remain bounded to the artwork glow and other dirty themed regions. Record any
input lag, full-screen flash, tearing, watchdog event, or heap discontinuity as
a qualification defect.

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
