# DeskWave Embedded Display Design System

## Status and authority

This design system has two explicit layers:

- **Current implementation** records what the checked-out firmware renders today.
- **Artwork-reactive target** records the requested enhancement direction for future design and implementation.

When they differ, current inputs, screen structure, and hardware limits remain authoritative. The artwork-reactive target supersedes the old static accent treatment, visible artwork frame, abrupt accent-state behavior, and the former 32 px Now Playing header. The Now Playing target is deliberately headerless.

## Product and rendering contract

DeskWave is a self-hosted Linux music controller rendered directly by an ESP32 to a landscape `320 × 240` ILI9341/XPT2046 panel. The UI uses LovyanGFX, 16-bit RGB565 color, built-in bitmap fonts, a 26 MHz SPI write clock, JPEG artwork stored in LittleFS, and resistive touch. There is no browser, DOM, CSS, alpha compositor, GPU blur, or arbitrary font loading.

The experience should read as a compact premium music appliance: calm, tactile, legible at arm's length, visually tied to the current record, and inexpensive to update. Album artwork is the hero. Controls remain conventional and immediately recognizable. Decorative color must never overpower metadata or obscure state.

## Canonical fixed canvas

The only breakpoint is the physical `320 × 240` landscape display. Do not design responsive alternatives unless a different panel is explicitly in scope.

| Region | Current geometry | Contract |
| --- | --- | --- |
| Now Playing status | compact overlay within `x 276–319`, `y 3–13` | A tiny linked/retry dot or glyph plus abbreviated text. It is not a bar, title, player-brand banner, or full-width surface. |
| Artwork | approximately `x 6`, `y 7`, `166 × 166` | Square JPEG or branded fallback; no visible frame, with only a restrained palette glow. |
| Metadata | approximately `x 180`, `y 7`, `134 × 166` | Track title, artist, honest queue preview, and enough inset at the top right for the compact link indicator. |
| Progress | `x 0–319`, `y 184–204` | `304 px` rail from `x 8`; position left and duration right. |
| Transport footer | visually `y 205–239`; touch begins at `y 190` | Five exact actions: Shuffle, Previous, Play/Pause, Next, More. |

Footer touch regions are fixed and must match the rendered dividers:

- Shuffle: `x 0–63`
- Previous: `x 64–117`
- Play/Pause: `x 118–201`
- Next: `x 202–255`
- More: `x 256–319`

The touch-only hardware also maps `x ≥ 238, y < 36` to More in the Playback control context (currently Now Playing and About) and Menu/next-screen in the Device, Settings, and Actions contexts. In Now Playing, keep that target visually honest with a tiny link-status treatment in its top-right corner, but do not draw a full-width header, `NOW PLAYING`, a Spotify/player logo, or the player name.

## Visual hierarchy

1. Artwork and track title form the primary reading pair.
2. Artist and playback state are secondary.
3. Progress and time are glanceable but quiet.
4. The central Play/Pause target dominates the control row.
5. Shuffle, Previous, Next, and More are equal-height secondary targets.
6. Queue information is explicit: show actual entries, `QUEUE EMPTY`, or `QUEUE UNAVAILABLE`; never fabricate an upcoming track.
7. Connection state is persistent but visually subordinate: a compact corner indicator, never a competing header.

## Current fallback palette

These source RGB values are encoded to RGB565 by `rgb565()` and remain the no-artwork/cold-boot fallback.

| Role | Source RGB | RGB565 | Use |
| --- | --- | --- | --- |
| Background | `#070A12` | `0x0042` | Full-screen base |
| Background lift | `#101926` | `0x10C4` | Subtle vertical atmosphere |
| Panel | `#121D29` | `0x10E5` | Cards and footer |
| Raised panel | `#1C2A39` | `0x1947` | Active controls and overlays |
| Primary text | `#F1F4F2` | `0xF7BE` | Titles and important values |
| Muted text | `#A9B5BE` | `0xADB7` | Labels and supporting copy |
| Primary accent | `#6FDAC2` | `0x6ED8` | Motion, progress, focus |
| Dim accent | `#225B56` | `0x22CA` | Low-intensity states |
| Player green | `#1DB954` | `0x1DCA` | Linked/player-specific signal |
| Secondary violet | `#A891DE` | `0xAC9B` | Secondary light and More |
| Ambient magenta | `#E098B2` | `0xE4D6` | Very low-intensity atmosphere |
| Warning | `#E8C077` | `0xEE0E` | Retry and caution |
| Error | `#F66969` | `0xF34D` | Failures and destructive action |
| Divider | `#373E69` | `0x31ED` | Rails, borders, separators |

Warning and error colors are semantic and must not be replaced by artwork colors. Linked status may use a small contrast-safe artwork-palette cue; there is no persistent Spotify logo or player-brand banner in the headerless Now Playing target.

## Artwork-reactive palette contract

The Linux host should derive a small, stable palette from the already-decoded artwork and send processed colors with the playback/artwork identity. The device consumes colors; it does not perform image analysis.

Required runtime roles:

| Runtime role | Purpose | Guardrail |
| --- | --- | --- |
| `primary` | Active icons, progress fill, primary halo | Chroma-limited and contrast-safe against the selected support background |
| `secondary` | Secondary halo, More/repeat detail, small atmospheric cues | Related to but visibly distinct from primary |
| `darkSupport` | Palette-tinted deep surface or halo sink | Must remain dark enough for the existing information hierarchy |
| `foregroundSupport` | Readable icon/text color when primary itself is unsuitable | Selected on the host from measured contrast, never guessed on-device |

The palette must be keyed to `artwork_id` or the same stable track/artwork identity used by the JPEG. A palette arriving late for a skipped track is ignored just as a late artwork download is ignored. If extraction fails, artwork is absent, or payload validation fails, use the fallback palette above.

The host should reject nearly identical primary/secondary candidates, avoid extreme neon colors, and choose support colors after measuring luminance. Aim for at least `4.5:1` contrast for small text and `3:1` for large symbols or control icons. Primary track and artist text normally remain neutral; use `foregroundSupport` only for small accent-bound labels or icons that need it.

## Artwork reliability and atomic presentation

Artwork and palette form one validated visual bundle, not two loosely timed decorations. Give every requested bundle both a stable `artworkId` and a monotonic `generation` (or equivalent request sequence). Track these states separately:

- `visibleBundle`: the last fully validated cover + palette currently on-screen.
- `pendingBundle`: cover bytes and palette fields accumulating for the next id/generation.
- `fallbackBundle`: the designed no-art cover + fallback palette, also treated as a complete bundle.

The transition contract is strict:

1. When track metadata changes, keep `visibleBundle` intact while the next cover and palette load. Do not clear the artwork rectangle, draw an empty panel, or momentarily swap to the generic placeholder.
2. Cover and palette may arrive independently, but neither becomes visible alone. Validate both against the same id/generation and commit them atomically.
3. Ignore any cover or palette whose id/generation is older than the pending request or does not match the authoritative playback artwork identity.
4. Once `pendingBundle` is complete, transition glow/icons/progress and replace the cover as one coordinated visual change. The old bundle remains the rollback-safe frame until that commit.
5. A transient download, decode, or palette-extraction failure retains the last validated bundle and remains retryable; it must not create a blank, broken-image, or mismatched cover/palette state.
6. Use `fallbackBundle` only when the host authoritatively reports that the track has no artwork (or on cold boot before any validated artwork), not merely because the next request is slow.
7. A genuine no-art track commits the deliberate branded fallback cover and fallback palette atomically, so it is still a complete designed state.

If implementation needs a terminal policy for permanently unavailable advertised artwork, it must still resolve to an explicit complete bundle after bounded retries; never expose a half-bundle. Palette lifetime, idle softening, and transition interpolation always operate on `visibleBundle`.

## Target color application

Use the active palette consistently but sparingly:

- Tint Play/Pause, Previous, Next, Shuffle, and More icon strokes/fills from the runtime roles. State is still visible through intensity and shape, not color alone.
- Tint repeat and mute indicators on their existing Actions/overlay surfaces.
- Use `primary` for the progress fill and scrub marker detail; keep the unfilled rail subdued.
- Use `primary` and `secondary` for the artwork halo.
- Use `darkSupport`, mixed with a restrained amount of `primary`, to color the entire exposed Now Playing canvas and its tonal surfaces. The result should resemble Spotify's artwork-derived lyrics backdrop: unmistakably related to the cover but always deep, muted, and text-safe rather than bright or literal.
- Keep title, artist, timestamps, warning, error, and destructive-action copy on their semantic neutral colors unless a measured support color is required.
- Keep only the tiny linked/retry indicator at the top right. Remove `NOW PLAYING`, Spotify/player identity, player name, and every other former header element.

Disabled or unavailable controls use muted neutral color, not a low-contrast version of the album accent. Active Shuffle/Repeat can use `primary`; inactive states use muted text/divider color.

## Artwork treatment

The target artwork presentation has **no visible border or frame**. Remove the final high-contrast rounded outline and avoid a card edge that boxes in the cover.

Simulate a soft edge glow with cheap concentric RGB565 shapes behind the JPEG:

1. An outer, low-intensity `secondary` halo offset by roughly `2–4 px`.
2. A tighter, slightly brighter `primary` halo around the cover perimeter.
3. The JPEG drawn cleanly on top at the enlarged headerless-layout size, approximately `166 × 166`.

The glow should read as light leaking from the cover, not as a colored stroke. Use a small number of preblended rectangles/rounded rectangles; do not require blur, per-pixel alpha, or repeated JPEG decoding. The fallback artwork receives the same treatment from the fallback palette.

## Theme transitions

Theme identity is independent of playback status. Maintain `currentPalette`, `startPalette`, and `targetPalette` on-device.

- Recommended transition duration: `750 ms`; permitted tuning range: `500–1000 ms`.
- Interpolate RGB565 channels or pre-expanded RGB channels with monotonic easing.
- Update bounded accent regions around `20 fps` (`50 ms` steps). Fifteen frames over `750 ms` are sufficient on this panel.
- Redraw only the halo patches, accent icon regions, progress accent, and other palette-colored pixels. Do not re-decode the JPEG or repaint neutral metadata every frame.
- On a track change, coordinate the palette transition with the existing metadata/artwork swap so there is no one-frame fallback flash.
- A palette-only update for the same artwork may transition in place; it must not restart title or progress state.

Keep the existing `220 ms` metadata fade/slide as a separate motion unless implementation deliberately unifies the timelines. The existing long-title behavior is a clipped, repeating horizontal marquee at `34 px/s` with a `33 ms` frame target; the headerless layout may widen or narrow its clip region but does not change the motion contract.

## Physical RGB synchronization

On the checked-in ESP32-D0WD-V3/CYD profile, the rear RGB LED is part of the artwork-reactive presentation rather than an independent status rainbow.

- Drive it from the same currently sampled and eased `visibleBundle` palette used to paint the display, normally the sampled `primary` role after pause/idle softening.
- Apply a conservative global brightness cap so the LED supports the screen instead of overpowering it; preserve the common-anode PWM inversion in the hardware adapter.
- Advance the LED through the same `750 ms` palette interpolation as the display. A track change must not make the screen show one palette while the physical RGB light shows another.
- Preserve semantic error signaling: a fatal/error blink may temporarily override palette sync. During boot, pairing, provisioning, or before any valid visible bundle exists, use the designed fallback palette or an existing semantic state treatment.
- When display brightness is dimmed for inactivity, scale the RGB LED down correspondingly. Do not continue a full-bright rainbow or unrelated hue cycle behind a dim screen.

## Paused and idle carryover

Paused, stopped, and temporarily disconnected states retain the most recent valid palette. They soften rather than reset:

- Recommended idle intensity: `55%` of playing intensity.
- Recommended saturation mix: `65%` palette color toward a luminance-matched neutral.
- Recommended glow strength: `35–45%` of playing glow.
- Use the same transition engine when entering or leaving the resting state.
- Resume restores full intensity smoothly; it does not request or snap to a new palette.

Do not discard the retained palette merely because title fields momentarily clear or the host is rediscovering. Replace it only when a new valid artwork palette becomes authoritative, after a deliberate reset, or on cold boot with no retained state.

## Typography

Use only firmware-available LovyanGFX bitmap faces:

- `fonts::Font4`: track title, large state/value, DeskWave wordmark.
- `fonts::Font2`: artist, card values, important labels.
- `fonts::Font0`: compact labels, timestamps, secondary instructions.
- `fonts::AsciiFont24x48`: pairing code only.

Text never wraps. `drawFitted()` truncates with an ellipsis to a fixed pixel width; the title is the exception because it renders inside a clip rectangle and scrolls when wider than `144 px`. Do not design multiline metadata that the renderer cannot support.

## Shape, spacing, and density

- Primary content gutter: `8–10 px`.
- Main inter-column gap: approximately `8 px` between the enlarged artwork and metadata panel.
- Card radii: typically `7–10 px`; large modal/connection surfaces `13–16 px`.
- Lines and dividers: `1 px` unless used as a tiny progress rail (`3 px`).
- Footer control height: `35 px`; central target width: `84 px`.
- Icons are simple LovyanGFX geometry—lines, triangles, circles, rectangles, and beziers—with no raster icon font dependency.

## Screen family

- **Boot:** branded atmospheric wave mark and version.
- **Connection:** pairing, provisioning, retry/error, or generic state card.
- **Now Playing / active:** artwork, metadata, queue, progress, and transport.
- **Now Playing / idle:** no-player or no-music card. If a palette was previously active, use its resting treatment.
- **Device:** network status and selectable MPRIS players.
- **Actions:** Shuffle and Repeat cards with a close footer.
- **Settings:** five-row selector for brightness, idle dim, volume step, default screen, and factory reset.
- **About:** firmware/protocol and device health.
- **Overlays:** volume/mute, toast/error, and factory-reset countdown; each supersedes underlying animation while visible.

Artwork-reactive theming is primarily a Now Playing concern. Other screens may borrow a muted current accent for continuity, but semantic controls, warnings, and readability take precedence.

## Idle Spotify ambient clock target

When the Now Playing route has no active media player, or a selected player has no active music,
replace the existing centered idle card with a full-canvas ambient clock. This target is specific to
the idle branch and does not add a Spotify logo, clock, or date to active playback.

### Information hierarchy and copy

1. A large current local time is the dominant glance target. Use a 12-hour clock with minutes and a
   clearly separated uppercase `AM` or `PM`; suppress a leading zero on the hour.
2. Render the local calendar date below it in the exact abbreviated-month format `Aug/31/2026`,
   generated as `%b/%d/%Y`.
3. Place a recognizable Spotify circle-and-three-arcs mark above or beside the time. Build it from
   circles and Bezier strokes already supported by LovyanGFX; do not use an image download or font
   glyph.
4. Keep the existing compact `LINK`/`RETRY` indicator in the top-right corner. It remains subordinate
   to the time and must not become a full-width header.
5. Do not show the old `DeskWave`, `No active media player`, or `No music playing` card copy in this
   target. The clock itself is the useful idle state.

### Composition

- Use the full `320 x 240` canvas with no enclosing card or persistent transport footer.
- Keep the Spotify mark in the upper visual third, the clock centered near the optical midpoint, and
  the date directly below with enough separation to read from arm's length.
- Use `fonts::Font4` for the largest practical clock digits, `fonts::Font2` for `AM`/`PM` and the date,
  and `fonts::Font0` only for the compact link state. Never introduce a new typeface.
- Protect a quiet central readability zone behind the clock. Animated paths can pass behind this zone
  only at very low contrast.

### Color and atmosphere

- Base the idle canvas on `kBackground` and `kBackgroundLift`.
- Use the existing player green `#1DB954` as the Spotify mark and primary luminous accent.
- Mix `kAccent` into near-green highlights and reserve `kViolet` for a very faint secondary edge so
  the result remains recognizably Spotify-led rather than a generic rainbow.
- Keep the time on `kText`, the date on a brightened `kTextMuted`, and preserve `kWarning` for `RETRY`.

### Motion keyframe and animation contract

The canvas design should show a representative mid-animation keyframe. The firmware motion is a slow,
continuous ambient loop rather than a screen saver bounce:

- Two or three broad curved wave ribbons travel horizontally across the canvas at different speeds.
  Their phase wraps seamlessly; direction never reverses and no element bounces.
- A restrained circular halo around the Spotify mark breathes over roughly `2400 ms`.
- Six to ten tiny particles drift horizontally with small fixed vertical offsets and wrap at the edge.
- The time updates once per minute; the date updates at local midnight. Redraw only their bounded text
  rectangles when values change.
- Target a `50 ms` animation cadence. Use preblended RGB565 colors and bounded dirty regions or an
  indexed sprite; never re-decode artwork, allocate per frame, or block touch/network work.
- Enter and leave idle with the existing theme transition engine: fade the ambient clock in when media
  stops and hand off cleanly to active artwork when playback starts.

## Performance and redraw rules

- Full-screen repaint is for screen/state transitions, not steady playback. The exposed artwork-derived canvas may update in a small number of coarse opaque regions during the short theme transition.
- Progress refreshes every `500 ms` and already repaints a bounded strip.
- Theme animation must use similarly bounded dirty regions.
- Never animate background stars, JPEG pixels, or all neutral text merely to show a palette transition.
- Update the RGB LED at the same bounded theme cadence and from the same sampled palette as the display; remove the unrelated continuous rainbow cycle.
- Cache converted RGB565 palette roles and idle variants; do not recompute palette extraction on the ESP32.
- Avoid broad gradients. Current atmosphere uses only 30 horizontal `8 px` bands and a few faint circles, drawn during a full render.
- Preserve input responsiveness: display work runs in the Arduino loop alongside bounded queue consumption while network, artwork, and touch have separate tasks.

## Tunable target constants

These names are design-level recommendations for a maintainable implementation:

| Constant | Recommended default | Purpose |
| --- | --- | --- |
| `kThemeTransitionMs` | `750` | Track/palette crossfade duration |
| `kThemeFrameMs` | `50` | Bounded accent repaint cadence |
| `kGlowPlayingStrength` | `100%` | Normal halo intensity |
| `kGlowIdleStrength` | `40%` | Resting halo intensity |
| `kIdlePaletteIntensity` | `55%` | Overall accent energy while paused/idle |
| `kIdleSaturation` | `65%` | Palette identity retained at rest |

## Non-negotiable design checks

- No visible old artwork border remains in the target state.
- The same accepted palette drives halo, active icons, and progress accents.
- The same sampled accepted palette drives the physical RGB LED, subject only to brightness scaling and semantic error override.
- The Now Playing screen has no full-width top bar, no `NOW PLAYING` label, no Spotify/player logo, and no player-name subtitle.
- Linked/retry status remains present as a tiny top-right indicator that occupies only a small corner of the content canvas.
- New palettes transition; they never snap or flash through fallback.
- The previous validated cover and palette remain visible while the next bundle loads.
- Cover and palette commit atomically by matching artwork id/generation; stale or partial bundles never render.
- Genuine no-art content uses a deliberate complete fallback bundle; loading/failure states never go blank or broken.
- Paused/idle states retain and soften the current palette.
- All text and icons remain readable over the actual RGB565 output.
- Footer visuals remain aligned with the exact touch hitboxes.
- No queue, capability, connection, or control state is invented for visual symmetry.
- The design remains feasible with LovyanGFX primitives and bounded redraws on the checked-in ILI9341 hardware profile.

## Synchronized lyrics and no-auto-dim target

This target supersedes the queue-specific and automatic-idle-dimming requirements above while preserving every unrelated geometry, palette, typography, transition, touch, artwork, and ambient-clock contract.

- Replace the active Now Playing queue card at approximately `x 187`, `y 88`, `120 x 76` with a synchronized lyrics card. Do not add a separate lyrics screen or displace artwork, title, artist, progress, or transport controls.
- Label the card `LYRICS` in the runtime secondary accent.
- Show a compact three-line window: previous lyric in subdued muted text, current lyric in foreground text with a slim runtime-primary accent marker, and next lyric in subdued muted text.
- Fit or truncate each lyric line inside the existing 106 px inner width. The firmware renderer remains single-line and allocation-free; do not introduce a scrolling paragraph or tiny wrapped copy.
- Use short honest states: `FINDING LYRICS`, `INSTRUMENTAL`, and `LYRICS UNAVAILABLE`. Never fabricate lyric text.
- Advance the current-line highlight from the authoritative playback position, respecting pause and seek. Redraw only the bounded lyrics-card region when the active line changes.
- Disable automatic TFT and rear-RGB idle dimming. Manual brightness remains available, and playback-status theme softening remains separate from inactivity dimming.
- Remove the Idle dim row from Settings once the behavior is disabled; preserve Brightness, Volume step, Default screen, and Factory reset.
