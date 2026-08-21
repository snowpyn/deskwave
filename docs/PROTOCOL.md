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
- `payload` must be a JSON object.
- Device-to-host WebSocket messages are limited to 16,384 bytes by the host.
  Host state/player messages are deliberately kept below the firmware's 8,192
  byte receive limit.
- Only UTF-8 text frames are valid application messages. Binary frames are
  rejected.

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
    "queue": false
  },
  "captured_at_ms": 1770000000000
}
```

Optional MPRIS properties use JSON `null`, never invented values. Firmware
renders a capability as unavailable when it is null/false. `status` is one of
`playing`, `paused`, or `stopped`; `repeat` is `off`, `track`, `playlist`, or
null. Volume is normalized to 0.0–1.0. Duration and position are milliseconds.

The host sends an immediate state when content changes and a periodic position
resynchronization while stable. Firmware records local receipt time and advances
position from its monotonic clock only while `status == "playing"`.

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

The host accepts source artwork only through its bounded processor. It resizes
and center-crops to 240×240 RGB, writes non-progressive JPEG at quality 82, and
names the object by SHA-256 of the rendered bytes. Firmware accepts only:

- lowercase 64-character hexadecimal IDs;
- paths beginning `/v1/artwork/` with no `..`;
- HTTP 200 `image/jpeg` responses with declared size 5–393,216 bytes;
- JPEG SOI (`FF D8`) and EOI (`FF D9`) markers.

Downloads use a temporary file and atomic rename. A malformed or unavailable
image leaves the branded placeholder visible and cannot block controls.

## Compatibility policy

Additive optional fields may be introduced within protocol 1. Existing field
meaning, units, and enumerated values must not change. A required incompatible
change increments `protocol`. Devices reject an unsupported version visibly
instead of attempting a best-effort interpretation.
