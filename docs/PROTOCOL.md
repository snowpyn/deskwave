# DeskWave protocol 1

Protocol 1 is a compact authenticated LAN protocol. JSON control/state travels
over one persistent WebSocket; pairing, health, artwork, and optional player
inspection use HTTP. All examples omit irrelevant HTTP headers for clarity.

## Discovery and transport

DeskWave Host advertises:

```text
Service type: _deskwave._tcp.local.
TXT protocol=1
TXT path=/v1/ws
Default TCP port: 8765
```

The ESP32 first uses a configured host override when present, otherwise queries
mDNS. Protocol 1 uses unencrypted `http://` and `ws://` on a trusted private LAN.
Authentication prevents unauthorised control but does not provide network
confidentiality.

## HTTP routes

| Method and path | Authentication | Purpose |
| --- | --- | --- |
| `GET /healthz` | None | Host/process/protocol health; contains no secret state |
| `POST /v1/pairing/request` | Pairing code | Register a pending physical device |
| `POST /v1/pairing/status` | Pairing code | Poll and consume an approved token |
| `GET /v1/artwork/{sha256}.jpg` | Bearer token | Retrieve normalized artwork |
| `GET /v1/players` | Bearer token | Diagnostic bounded player list |
| `GET /v1/ws` | Bearer token + upgrade | Real-time state and controls |

Protected requests use:

```http
Authorization: Bearer <random-device-token>
```

Pairing bodies are limited to 2,048 bytes. HTTP responses carrying tokens use
`Cache-Control: no-store`. Artwork responses are `image/jpeg`, immutable by
content hash, and sent with `X-Content-Type-Options: nosniff`.

## Pairing

The ESP32 creates a random six-digit code and a stable device ID derived from
its eFuse MAC suffix. It submits:

```http
POST /v1/pairing/request
Content-Type: application/json

{
  "device_id": "dw-aabbccdd",
  "name": "DeskWave ccdd",
  "code": "123456"
}
```

The host returns HTTP 202 while pending. A user approves the code locally with
`deskwave-host pair 123456`. The ESP32 polls using a POST body so the code never
appears in access-log query strings:

```json
{
  "device_id": "dw-aabbccdd",
  "code": "123456"
}
```

Pending response:

```json
{"status":"pending"}
```

Collected approval:

```json
{"status":"paired","token":"<random token>"}
```

Requests, codes, and uncollected approvals expire after five minutes. The
approval row is deleted when collected. The paired-device database stores the
token hash; firmware stores the token in NVS. Pairing endpoints are rate-limited
per source address.

## WebSocket envelope

Every text message uses this top-level shape:

```json
{
  "protocol": 1,
  "type": "playback_state",
  "sequence": 182,
  "timestamp_ms": 1770000000000,
  "utc_offset_seconds": -14400,
  "payload": {}
}
```

Rules:

- `protocol` must equal integer `1`.
- `type` is a non-empty string no longer than 32 characters.
- `sequence` is an integer from 0 through 2,147,483,647. Each sender owns its
  sequence space and wraps to zero.
- `timestamp_ms` is optional and non-negative. Host timestamps are Unix epoch
  milliseconds; firmware timestamps are monotonic milliseconds since boot.
- Host messages include `utc_offset_seconds`, bounded to plus or minus 24 hours,
  so the display can derive host-local civil time without embedding timezone or
  daylight-saving policy in firmware. Device messages may omit it.
- `payload` must be a JSON object.
- Device-to-host WebSocket messages are limited to 16,384 bytes by the host.
  Host state/player messages are deliberately kept below the firmware's 8,192
  byte receive limit.
- Only UTF-8 text frames are valid application messages. Binary frames are
  rejected.

Before serialization, the host shapes device-facing playback metadata to the
firmware's fixed buffers: ordinary text and IDs are at most 128 UTF-8 bytes,
player names are at most 64 bytes, at most three artists share one 128-byte
joined budget, and at most four queue entries are sent with 128-byte fields.
Truncation preserves UTF-8 character boundaries. Device frames use compact
UTF-8 JSON rather than ASCII escaping, and the production encoder enforces the
8,192-byte limit after escaping.

Three consecutive malformed device messages close the session with a policy
violation. A single malformed host message is ignored by firmware and logged;
the connection remains recoverable.

## Host-to-device messages

### `hello`

Sent first after authentication:

```json
{
  "host_version": "0.1.0",
  "protocol": 1,
  "device_id": "dw-aabbccdd",
  "heartbeat_seconds": 20
}
```

### `clock_sync`

Sent every 30 seconds while no playback update is pending. Its payload is empty;
the authoritative Unix time and current host-local UTC offset are carried in the
envelope. The ESP32 advances that sample with its monotonic clock between syncs.

### `playback_state`

```json
{
  "title": "Track title",
  "artists": ["Artist one", "Artist two"],
  "album": "Album",
  "duration_ms": 245000,
  "position_ms": 91250,
  "status": "playing",
  "artwork_id": "64-lowercase-hex-characters-or-null",
  "artwork_path": "/v1/artwork/<hash>.jpg",
  "artwork_generation": 27,
  "theme": {
    "primary": 3718648,
    "secondary": 10980346,
    "background": 1054759,
    "foreground": 16251644
  },
  "volume": 0.72,
  "muted": false,
  "shuffle": true,
  "repeat": "playlist",
  "player_id": "org.mpris.MediaPlayer2.spotify",
  "player_name": "Spotify",
  "track_id": "/org/mpris/MediaPlayer2/Track/123",
  "capabilities": {
    "seek": true,
    "next": true,
    "previous": true,
    "control": true,
    "queue": true
  },
  "queue": [
    {
      "title": "Next track",
      "artist": "Artist two",
      "track_id": "/org/mpris/MediaPlayer2/Track/124"
    },
    {
      "title": "Following track",
      "artist": "Artist three",
      "track_id": "/org/mpris/MediaPlayer2/Track/125"
    }
  ],
  "captured_at_ms": 1770000000000
}
```

Optional MPRIS properties use JSON `null`, never invented values. The `queue`
array contains at most four upcoming tracks when the active player exposes the
standard MPRIS TrackList interface; `capabilities.queue` is false when that
interface is unavailable and the array is empty when the queue is currently
empty. Firmware renders a capability as unavailable when it is null/false.
`status` is one of
`playing`, `paused`, or `stopped`; `repeat` is `off`, `track`, `playlist`, or
null. Volume is normalized to 0.0–1.0. Duration and position are milliseconds.

The host sends an immediate state when content changes and a periodic position
resynchronization while stable. Firmware records local receipt time and advances
position from its monotonic clock only while `status == "playing"`.

`theme` and `artwork_generation` are additive protocol-1 fields. Each theme
color is an integer from 0 through 16,777,215 representing packed sRGB
`0xRRGGBB` with no alpha channel; JSON writes the value in decimal because JSON
has no hexadecimal-number syntax. `primary` drives the dominant glow and
progress accent, `secondary` supports secondary controls, `background` is the
dark artwork-derived support color, and `foreground` is contrast-corrected for
readable text and icons.

`artwork_generation` is an unsigned 32-bit host-issued identity for one atomic
artwork/theme publication; zero means no host visual tuple has been promoted.
The `artwork_id`, `artwork_path`, and `theme` in a snapshot all belong to that
generation. A new track/artwork intent receives a new nonzero generation, but
pending snapshots continue carrying the previous validated tuple and its
previous generation. Resolution promotes the new JPEG/theme or authoritative
fallback atomically. Abandoned rapid-skip intents can leave gaps, so clients
compare the value only for equality and must not infer timing from it.

A downloaded result is committed only while its generation still matches the
latest playback state, which prevents an old request from winning after rapid
skips. Position updates, pause, short reconnects, and temporary metadata gaps do
not clear the promoted tuple. When a track is confirmed to have no usable
cover, `artwork_id` and `artwork_path` are null and the atomically promoted
`theme` carries the deliberate fallback palette.

Peers that omit or do not understand the additive fields remain compatible:
firmware uses its fallback palette when `theme` is absent or invalid, and an
older device ignores the extra members.

### `command_result`

Every validated control command produces a result:

```json
{
  "request_sequence": 44,
  "command": "toggle",
  "success": false,
  "error": "active media player does not accept controls"
}
```

The host caches the latest 64 results per WebSocket session by request sequence.
A retried sequence receives the cached result rather than executing a duplicate
media command.

### `players`

Response to `list_players`, limited to six entries:

```json
{
  "request_sequence": 43,
  "players": [
    {
      "id": "org.mpris.MediaPlayer2.vlc",
      "name": "VLC media player",
      "status": "paused"
    }
  ]
}
```

### `error`

Protocol errors are explicit:

```json
{
  "code": "invalid_message",
  "message": "unsupported protocol version"
}
```

The host can also answer `ping` with `pong`, preserving a scalar nonce and
including `request_sequence`.

## Device-to-host messages

### `control`

No-argument commands:

```text
play, pause, toggle, previous, next,
volume_up, volume_down, mute, shuffle_toggle, refresh
```

```json
{"command":"toggle"}
```

Argument-bearing commands:

```json
{"command":"set_volume","value":0.65}
{"command":"seek","offset_ms":-10000}
{"command":"set_repeat","mode":"track"}
{"command":"select_player","player_id":"org.mpris.MediaPlayer2.vlc"}
```

Validation bounds:

- volume: 0.0 through 1.0;
- relative seek: −1,800,000 through +1,800,000 ms;
- repeat: `off`, `track`, or `playlist`;
- player ID: non-empty, at most 255 characters.

Backend calls are bounded and all failures return `command_result`; unsupported
MPRIS operations never silently succeed.

### `list_players`

```json
{}
```

The envelope type is `list_players`; it is not a control command. The response
is a `players` message tied to the request sequence.

### Reserved device messages

Protocol 1 validates `ping` and `device_status` for forward-compatible health
use. The current firmware relies on WebSocket heartbeat control frames and does
not need to emit periodic application pings.

## Artwork contract

The host accepts `file://`, HTTP, and HTTPS source artwork only through its
bounded processor. It retrieves with bounded retry/backoff for temporary
failures, validates the complete transfer before use, resizes and center-crops
to 320×320 RGB, writes a high-quality non-progressive JPEG with 4:4:4 chroma,
extracts the four-color theme, and stores both as one content-addressed cache
bundle. `artwork_id` is the SHA-256 of the exact rendered JPEG bytes. Firmware
accepts only:

- lowercase 64-character hexadecimal IDs;
- a path exactly equal to `/v1/artwork/<artwork_id>.jpg`;
- HTTP 200 `image/jpeg` responses with declared size 5–393,216 bytes;
- JPEG SOI (`FF D8`) and EOI (`FF D9`) markers;
- completed files whose SHA-256 equals `artwork_id` and whose
  `artwork_generation` is still current.

Downloads use a temporary file and atomic rename. The previously validated
cover remains visible until a matching replacement commits. Malformed,
incomplete, unavailable, or stale images are rejected without blocking
controls; a branded placeholder is selected deliberately only after the host
confirms that the current track has no usable artwork.

## Compatibility policy

Additive optional fields may be introduced within protocol 1. Existing field
meaning, units, and enumerated values must not change. A required incompatible
change increments `protocol`. Devices reject an unsupported version visibly
instead of attempting a best-effort interpretation.
