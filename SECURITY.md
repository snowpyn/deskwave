# Security policy

## Supported versions

| Version | Security updates |
| --- | --- |
| 0.1.x | Supported |
| Older/unreleased snapshots | Not supported |

## Reporting a vulnerability

Use GitHub's **Report a vulnerability** / private Security Advisory flow for
<https://github.com/snowpyn/deskwave>. Do not open a public issue containing a
working exploit, bearer token, Wi-Fi credential, private artwork URL, device
database, or identifying LAN details.

Include:

- affected commit/version and component (host, protocol, firmware, installer);
- threat model and required network/physical access;
- minimal reproduction steps or proof of concept;
- impact and any known mitigations;
- sanitized logs if relevant.

Expect an acknowledgement through the advisory within seven days. Timelines for
validation, fix, and disclosure depend on severity and hardware requirements.

## Deployment model

DeskWave `0.1.x` is for a trusted private LAN. The host binds to all interfaces
by default so the physical device can reach it. Keep TCP 8765 limited to trusted
local clients and do not port-forward or publicly expose it.

Pairing and all protected operations are authenticated, but protocol 1 uses
plain HTTP/WebSocket. Anyone able to passively inspect the trusted LAN could see
metadata and bearer tokens. Use a trusted WPA2/WPA3 network with client access
controls. A future protocol can add mutually authenticated transport without
weakening the current explicit pairing boundary.

ESP32 Preferences/NVS stores the device token and Wi-Fi credential. The
reference hobby/development configuration does not enable ESP32 Secure Boot,
flash encryption, or encrypted NVS. An attacker with physical flash/debug access
may recover them. A commercial deployment must evaluate Espressif Secure Boot,
flash encryption, encrypted NVS, production key provisioning, debug-port policy,
and recovery before treating the hardware as tamper resistant.

## Implemented controls

- Per-device random bearer tokens; no compiled/default secret.
- Six-digit, five-minute, explicitly approved pairing with source rate limits.
- Paired-device database stores token hashes; pending plaintext approval tokens
  exist only until collection/expiry.
- Device revocation and automatic firmware re-pair recovery.
- Strict JSON envelope/type/sequence/field validation and bounded message sizes.
- Three-strike policy for malformed WebSocket device messages.
- Bounded HTTP, D-Bus, artwork, pairing, and reconnect operations.
- Artwork URL credential rejection, redirect/byte/pixel limits, atomic cache,
  and private/special HTTP destination blocking by default.
- Cache/state directories restricted to the user and a sandboxed `systemd --user`
  service with no new privileges and read-only home outside DeskWave paths.
- Provisioning AP uses a per-boot password and nonce; settings never log
  credentials or tokens.
- Factory reset requires a two-step settings confirmation or a five-second
  three-button chord.

## Known security limitations

- No TLS or message-level encryption on the trusted LAN.
- No hardware-backed token storage, Secure Boot, or flash encryption in the
  reference build.
- The provisioning page is HTTP inside the temporary WPA-protected AP.
- Host health and pairing-request routes are intentionally unauthenticated;
  their output/input is bounded and contains no paired token.
- Enabling `allow_private_artwork_hosts` weakens SSRF protection and should be
  limited to trusted MPRIS applications.
- The host is a user-session service: compromise of that desktop user can
  control media and read DeskWave state, as expected for an MPRIS bridge.

## Secret handling

Never commit `.env`, `config.local.*`, Wi-Fi values, tokens, SQLite runtime
state, cache contents, firmware dumps from paired hardware, or diagnostic logs
that contain private data. The repository ignore rules cover common paths, but
contributors must still inspect staged changes before publishing.
