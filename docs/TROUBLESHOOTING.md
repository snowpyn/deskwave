# Troubleshooting

Start with observable state. The TFT presents product-level errors; serial and
the user-service journal provide structured component logs without exposing
tokens or Wi-Fi passwords.

```bash
journalctl --user -u deskwave-host -n 100 --no-pager
deskwave-host status
deskwave-host doctor
curl --fail http://127.0.0.1:8765/healthz
pio device monitor -b 115200 --port /dev/ttyACM0
```

Replace the serial port with the one from `pio device list`. A serial monitor is
diagnostic only; DeskWave does not require it in normal operation.

## Display stays dark

1. Disconnect power and verify VCC/GND against the display module datasheet.
2. Confirm TFT CS 15, DC 2, no-connected reset, SCLK 14, MOSI 13, and
   backlight 21.
3. Determine whether the breakout's backlight input is active-low and set
   `kBacklightInverted` accordingly.
4. Confirm the build target is `esp32-d0wd-v3` and the panel really uses an
   ILI9341 controller.
5. Check serial for `Display initialization failed`.

Do not repeatedly power a hot board or a circuit with an unexplained supply
short.

## Encoder direction or buttons are wrong

- Reverse A/B (GPIO 4/5) if rotation direction is opposite.
- Contacts must close to ground; firmware enables internal pull-ups.
- A short press fires on release. Holding for the configured long threshold
  produces one long event and suppresses the short event.
- Left/Right seek repeats only after a recognized long press.
- Check the input queue warning in serial if electrical noise is generating an
  abnormal event rate.

Do not remove debounce or reduce encoder edge filtering before checking wiring
and ground integrity.

## Provisioning AP is not visible

- Provisioning starts only when NVS has no valid Wi-Fi credentials.
- The SSID is `DeskWave-xxxx`; wait several seconds after reset.
- Confirm the board supports 2.4 GHz Wi-Fi (ESP32 does not join a 5 GHz-only
  network).
- To deliberately erase configuration, hold Left + Right + Menu for five full
  seconds and wait for the restart confirmation.

The AP uses a new password on each provisioning boot. Use the value currently
shown on the display.

## Wi-Fi unavailable / wrong password

DeskWave remains in Offline and retries with backoff; it should not reboot.
Verify:

- exact SSID capitalization;
- WPA password length and content;
- 2.4 GHz availability and signal strength;
- client isolation/captive portal policies;
- DHCP capacity.

If credentials are wrong, use the deliberate factory-reset chord and provision
again. NVS never logs the submitted password.

## ESP32 cannot find DeskWave Host

On the desktop:

```bash
systemctl --user status deskwave-host
deskwave-host doctor
ss -ltnp | grep ':8765'
```

Then check:

- both devices are on the same trusted LAN/VLAN;
- Wi-Fi access-point isolation is disabled;
- multicast UDP 5353 is permitted;
- TCP 8765 is permitted from the device network;
- another process is not occupying the configured port;
- the host has a non-loopback IPv4 route.

The service advertises `_deskwave._tcp.local.`. From another Linux machine,
`avahi-browse -rt _deskwave._tcp` can independently confirm discovery when
Avahi tools are installed.

If the journal reports `Address family not supported by protocol`, reinstall
the current user-service unit. Its `RestrictAddressFamilies` list must include
`AF_NETLINK`, which Zeroconf uses only to inspect local network interfaces.

## Pairing code never appears

Pairing begins only after Wi-Fi and host discovery. Resolve the state shown
before Pairing first. If an old token is still valid, the device connects without
showing a new code. Revoke it intentionally to exercise re-pairing:

```bash
deskwave-host devices
deskwave-host revoke dw-aabbccdd
```

The firmware detects a healthy host rejecting its session, clears the stored
token after the bounded connection window, and shows a new code.

## Pairing approval fails

- Run `deskwave-host pair` to confirm the request is pending.
- Approve the exact six ASCII digits shown on the device.
- Codes expire after five minutes; wait for a new device request after expiry.
- A code shared by multiple simultaneous pending devices is rejected as
  ambiguous rather than approving the wrong device.
- Check the daemon and CLI use the same XDG state directory/user account.

## Host says no MPRIS players

Run:

```bash
deskwave-host doctor
busctl --user list | grep org.mpris.MediaPlayer2
```

Start playback in a compatible desktop application. Browser media sessions may
appear only while a tab is actively presenting media. Sandboxed applications
may require their desktop integration/portal. DeskWave cannot synthesize
metadata that the player does not expose.

For phone playback, the phone session must be bridged into this same desktop
D-Bus (for example by KDE Connect). DeskWave cannot directly inspect Android or
iOS media sessions over Wi-Fi.

## Track information is missing

Inspect the active player's MPRIS properties:

```bash
busctl --user get-property \
  org.mpris.MediaPlayer2.spotify \
  /org/mpris/MediaPlayer2 \
  org.mpris.MediaPlayer2.Player Metadata
```

Replace the bus name. Empty artist, album, duration, volume, shuffle, repeat, or
artwork means that property was unavailable or rejected; the UI shows a neutral
fallback instead of fake data. Use the Device screen to select the intended
player if several are present.

## Artwork remains a placeholder

Host logs identify retrieval/validation failures without logging full sensitive
URLs. Common causes:

- the MPRIS player exposes no `mpris:artUrl`;
- a local file disappeared or is too large;
- the remote response is not an image;
- the source exceeds byte/pixel/redirect limits;
- a private HTTP artwork host is blocked by the default SSRF policy;
- the ESP32 rejects a download over 384 KiB or a malformed JPEG;
- LittleFS could not mount or commit the cache file.

Do not enable `allow_private_artwork_hosts` merely to hide an unrelated error.
If it is genuinely required, enable it only for trusted players and networks.

## Controls show a rejection

MPRIS capabilities differ by application. The host always returns success or a
reason. Typical failures are `no active media player`, `does not accept
controls`, `seek is not supported`, or a D-Bus timeout. The visible optimistic
feedback is reconciled by the next confirmed snapshot.

If every command fails, confirm the Device screen shows Host connected and that
the selected application reports `CanControl=true`.

## Host service restart loop

```bash
systemctl --user status deskwave-host
journalctl --user -u deskwave-host -b --no-pager
systemd-analyze --user verify ~/.config/systemd/user/deskwave-host.service
```

The supported installer creates writable DeskWave XDG directories before the
service sandbox starts. If the unit was copied manually, create:

```bash
install -d -m 0700 ~/.config/deskwave ~/.cache/deskwave ~/.local/state/deskwave
```

Confirm `~/.local/bin/deskwave-host` resolves to the dedicated virtual
environment and `deskwave-host run` works in the same desktop session.

## Reconnect after suspend is slow

The host service, D-Bus, mDNS, Wi-Fi, and DHCP may resume at different times.
DeskWave intentionally uses bounded retry/backoff to avoid reconnect storms.
Normal recovery requires no ESP32 reboot. If it never recovers:

1. verify the host health endpoint after wake;
2. check the host LAN address changed and mDNS republished it;
3. inspect Wi-Fi RSSI/IP on Device;
4. restart only the host service to isolate host vs firmware behavior;
5. preserve serial/journal logs for the smoke-test record.

## Factory reset did not run

Settings reset is intentionally difficult to trigger:

- Settings method: select Factory reset, press to arm, then hold the encoder.
- Recovery method: hold Left + Right + Menu together until the five-second
  countdown reaches zero.

Releasing any chord button cancels the recovery countdown. If Preferences/NVS
cannot be cleared, firmware reports failure and does not claim a successful
reset.

## Reporting a defect

Do not publish pairing tokens, Wi-Fi passwords, private artwork URLs, or a device
database. Include the DeskWave version, board/display model, host distribution,
player bus name, reproduction steps, sanitized logs, and whether the issue was
automated or physically reproduced. Security reports follow
[SECURITY.md](../SECURITY.md).
