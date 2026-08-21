# DeskWave Host

DeskWave Host is the Linux bridge between standard MPRIS media players and a
paired DeskWave controller. It runs without root in the desktop user's session,
uses the session D-Bus, publishes mDNS discovery, and serves an authenticated
HTTP/WebSocket API on the local network.

## Requirements

- Linux desktop with a session D-Bus
- Python 3.11 or newer
- An MPRIS-compatible player for media state and controls
- LAN multicast for automatic mDNS discovery, or a firmware host override

Spotify developer credentials are not required. The Linux Spotify application,
VLC, Firefox, Chromium-based players, and other MPRIS applications can work
through their standard desktop interfaces; each application decides which
capabilities it exposes.

## Isolated installation

From the repository root:

```bash
./host/scripts/install.sh --enable
~/.local/bin/deskwave-host doctor
```

This creates `~/.local/share/deskwave/venv`, installs the exact runtime
dependencies declared in `pyproject.toml`, creates a CLI symlink, installs the
user service, and preserves any existing `~/.config/deskwave/config.toml`.
The service starts automatically with the user session. To start it at boot
before interactive login, enable lingering once with `loginctl enable-linger
$USER`.

Service operations:

```bash
systemctl --user status deskwave-host
systemctl --user restart deskwave-host
journalctl --user -u deskwave-host -f
systemctl --user disable --now deskwave-host
```

For development:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -e 'host[dev]'
.venv/bin/deskwave-host run
```

## Pairing and device administration

The device must first discover the host and display a code.

```bash
deskwave-host pair              # list pending devices
deskwave-host pair 123456       # approve the displayed code
deskwave-host devices
deskwave-host revoke dw-aabbccdd
```

Codes and uncollected approvals expire in five minutes. A collected token is
stored as a SHA-256 hash in the SQLite paired-device record. Revoking a device
causes its firmware to detect the rejected session, discard its stored token,
and return to pairing.

## Configuration

Copy [`config.example.toml`](config.example.toml) to
`~/.config/deskwave/config.toml`. The installer does this only when no file is
already present.

```toml
[host]
bind = "0.0.0.0"
port = 8765
service_name = "DeskWave Host"
log_level = "INFO"
allow_private_artwork_hosts = false
artwork_max_bytes = 8388608
artwork_cache_bytes = 134217728
```

Supported environment overrides are `DESKWAVE_BIND`, `DESKWAVE_PORT`,
`DESKWAVE_LOG_LEVEL`, and `DESKWAVE_PREFERRED_PLAYER`. The packaged systemd
unit does not load the repository `.env.example`; use a user-service override
if environment configuration is required:

```bash
systemctl --user edit deskwave-host
```

```ini
[Service]
Environment=DESKWAVE_LOG_LEVEL=DEBUG
```

Run `systemctl --user daemon-reload && systemctl --user restart deskwave-host`
after changing an override.

## Runtime data

- Configuration: `~/.config/deskwave/`
- Resized artwork: `~/.cache/deskwave/artwork/`
- Device database: `~/.local/state/deskwave/devices.sqlite3`

Directories are mode `0700`, and the SQLite file and cache objects are mode
`0600`. The service intentionally avoids systemd mount-namespace restrictions:
on Ubuntu they put user services under the `unprivileged_userns` AppArmor
profile, which confined Snap players such as Spotify reject for MPRIS calls.
The remaining service hardening includes no-new-privileges, a restricted socket
family set, personality locking, SUID/SGID restrictions, and W^X enforcement.

## Player selection

The MPRIS backend applies a deterministic policy:

1. honor a manually selected/preferred player when it is playing;
2. otherwise retain the previously active player when it is playing;
3. otherwise choose a playing player by stable player ID;
4. when nothing is playing, use the configured preference, then the previous
   player, then stable player ID.

The ESP32 Device screen can request up to six detected players and select one.
The preference lasts for the host process lifetime; set `preferred_player` in
the TOML file for a startup preference.

DeskWave controls a phone only when a desktop bridge publishes that phone's
media session as an MPRIS player. KDE Connect is one common route. The ESP32 is
still only a controller/display: audio remains on the selected phone or PC.

MPRIS exposes shuffle as a boolean and repeat as off/track/playlist. Smart
Shuffle is a proprietary player mode with no supported MPRIS command, so the
host never reports a fake success for it.

## Artwork safety

Artwork is read from MPRIS `mpris:artUrl`. Local `file://` paths and public
HTTP(S) sources are supported. Before decoding, the host enforces byte, redirect,
DNS, address, and pixel limits. Private/special-purpose HTTP destinations are
blocked by default to prevent an untrusted media application from turning the
host into a LAN probe. Images are normalized to non-progressive 240×240 JPEG,
content-addressed, and evicted to the configured cache budget.

Set `allow_private_artwork_hosts = true` only if a trusted player genuinely
serves artwork from a private address and the SSRF tradeoff is understood.

## Diagnostics

```bash
deskwave-host status
deskwave-host doctor
curl --fail http://127.0.0.1:8765/healthz
```

`doctor` checks configuration directories, local service health or bind
availability, LAN address discovery, the session D-Bus, and current MPRIS
player detection. See the root [Troubleshooting guide](../docs/TROUBLESHOOTING.md)
for firewall, mDNS, metadata, and systemd diagnosis.

## Development checks

```bash
ruff format --check host scripts
ruff check host scripts
mypy host/src
pytest -q host/tests --ignore=host/tests/integration
pytest -q host/tests/integration
```

The integration suite exercises pairing, authorization, WebSocket state,
control idempotency, player listing, disconnect, and reconnect against the real
aiohttp server with an isolated backend and database. It does not fabricate a
production MPRIS path; actual D-Bus/media behavior belongs to the physical
system smoke test.
