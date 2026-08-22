# DeskWave Now Playing — 320x240 UI System

## Product and rendering context

DeskWave is a tactile ESP32 music controller with a 320x240 landscape ILI9341 TFT. The screen is rendered directly with LovyanGFX in RGB565; it is not a browser UI. The now-playing view must feel premium and immediately legible at arm's length while remaining cheap to redraw over SPI.

The persistent information hierarchy is:

1. Informational player identity and connection state.
2. Album artwork.
3. Complete track title and artist.
4. Honest queue availability.
5. Progress and time.
6. Five exact touch actions: Shuffle, Previous, Play/Pause, Next, More.

Never show volume or repeat on the now-playing screen. Never invent a next track when queue data is unavailable. The top-right header area is informational, not a touch button.

## Visual direction

Use a compact neural-noir music-console aesthetic: near-black blue ink, subtle atmospheric teal/violet light, precise hairlines, and one vivid Spotify-green signal. Interpret glass as opaque layered RGB565 panels with borders; do not rely on blur, transparency, photos beyond album artwork, or expensive effects.

- Background: `#070A12`, with very subtle vertical lift toward `#101926`.
- Primary panel: `#121D29`; raised control: `#1C2A39`.
- Primary text: `#F1F4F2`; secondary text: `#A9B5BE`.
- Spotify/player signal: `#1DB954`.
- Motion/accent: sea-glass `#6FDAC2`.
- Secondary light: soft violet `#A891DE`.
- Dividers: indigo charcoal `#373E69`.
- Avoid broad rainbow gradients, generic blue/purple tech gradients, or decorative color that competes with album artwork.

Use the firmware's existing bitmap fonts only. Use the largest available sans bitmap face for the title, a medium face for the artist, and the smallest face with generous spacing for labels. No serif or ornamental fonts.

## Exact 320x240 composition

- Header: `x=0..319`, `y=0..31`. Left: 18px Spotify/player glyph. Beside it: `NOW PLAYING` with the real player name below. Right: a small unboxed connected/retrying status; it must not resemble a button.
- Artwork card: approximately `x=8..139`, `y=38..169`, with a restrained offset glow and rounded border.
- Metadata card: approximately `x=150..311`, `y=38..169`.
  - Put the track title around `y=56`, lower than the current header-adjacent position.
  - Confine the title to one clipped strip. Long titles ping-pong horizontally so both ends can be read; short titles remain still.
  - Artist sits below the title with clear separation.
  - The former album/duplicate-title area becomes an honest `UP NEXT / QUEUE NOT SHARED` state until real queue data exists.
- Progress band: `y=174..194`, spanning nearly the full width, with elapsed and duration at opposite ends.
- Touch footer: exactly `y=195..239`, with visible regions matching firmware hitboxes:
  - Shuffle `x=0..63`
  - Previous `x=64..117`
  - Play/Pause `x=118..201`
  - Next `x=202..255`
  - More `x=256..319`

The central Play/Pause target is visually dominant. Shuffle shows an active green state. More uses a clear ellipsis plus `MORE` label and opens/closes the Actions surface. Previous and Next use conventional skip icons, not ambiguous arrows.

## Motion and redraw constraints

- Animate only the clipped title strip during steady playback, at roughly 20–25 frames per second.
- Title motion uses a calm side-to-side ping-pong with a brief readable dwell at each edge; no marquee wrap, bounce, vertical movement, or full-screen repaint.
- Progress may refresh twice per second.
- Track changes may retain the existing short metadata fade/slide transition.
- Static header, artwork, metadata chrome, and footer controls are drawn once until their data changes.

## Accessibility and interaction

- Maintain high text contrast and at least 44px footer touch height.
- Every visible footer action must map one-to-one to its exact hit region.
- Do not imply touch interaction in the header.
- Truncation is acceptable for artist/player labels, but never for the final readable state of a track title; title motion must expose the complete string.
