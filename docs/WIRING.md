# ESP32-D0WD-V3 Revision 3.1 wiring

The published `esp32-d0wd-v3` profile targets the locally verified
**ESP32-D0WD-V3 Revision 3.1** board and a 2.8-inch-class SPI **ILI9341** panel
with an XPT2046-compatible resistive touch overlay. The controller uses 3.3 V
logic. Check the exact display breakout's regulator, backlight resistor/driver,
touch-controller labels, and pin labels before wiring; bare panels and
5 V-tolerant breakout boards are not electrically equivalent.

Disconnect USB and external power while changing wiring. All modules must share
ground. Never power the ESP32 simultaneously from unrelated supplies unless the
power design explicitly supports it.

## Display

| ILI9341 signal | ESP32 GPIO | Firmware symbol | Notes |
| --- | ---: | --- | --- |
| SCLK / CLK | 14 | `kDisplaySclk` | SPI clock, 26 MHz write target |
| MOSI / SDI | 13 | `kDisplayMosi` | ESP32 to display data |
| MISO / SDO | 12 | `kDisplayMiso` | Display to ESP32; retain for readable/shared panels |
| CS | 15 | `kDisplayCs` | Active-low chip select |
| DC / RS | 2 | `kDisplayDc` | Data/command select |
| RST / RESET | Not connected (`-1`) | `kDisplayReset` | Panel reset is not wired |
| LED / BL | 21 | `kBacklight` | PWM control input, not an unbounded LED power feed |
| GND | GND | — | Required common ground |
| VCC | Per breakout | — | Use the voltage specified by the module vendor |

The physical panel is 240×320 and firmware uses rotation `1` for the required
320×240 landscape screen orientation on the Revision 3.1 loom. The touch
coordinates use the separately calibrated swapped-axis and 180-degree inverse
transform so the controls remain aligned with the touch overlay.
If colors are swapped, orientation is wrong, or the module inverts brightness,
change only the panel flags/rotation in
`firmware/include/config/hardware_config.h` and the display adapter—not UI
coordinates or application logic.

## Touch controller

The touch panel replaces the encoder and button loom:

| Signal | ESP32 GPIO |
| --- | ---: |
| TFT CS | 15 |
| TFT DC | 2 |
| TFT SCLK | 14 |
| TFT MOSI | 13 |
| TFT MISO | 12 |
| TFT reset | Not connected (`-1`) |
| TFT backlight | 21 |
| Touch CS | 33 |
| Touch IRQ | 36 |
| Touch DIN | 32 |
| Touch MISO | 39 |
| Touch CLK | 25 |
| RGB LED red (active-low) | 4 |
| RGB LED green (active-low) | 16 |
| RGB LED blue (active-low) | 17 |

The five footer bands activate
shuffle, previous, play/pause, next, and More; vertical swipes act as encoder
turns, and horizontal swipes act as previous/next. The touch calibration uses
200–3900 ADC endpoints. A tap is
classified only after three spatially stable samples, preventing the noisy
first ADC sample from collapsing every target into the center play/pause area.

The board's common-anode RGB LED is independent of the TFT backlight on
GPIO 21. DeskWave drives its three active-low channels from the same eased
final artwork-derived canvas background used by the Now Playing screen.
Firmware converts that sRGB color to linear PWM and applies the channel-balance
constants in `hardware_config.h` for the verified board. The RGB output is
brightness-capped, scales down when the display idles, and may flash red for a
semantic Error state. The TFT backlight remains a steady single-color brightness
channel; "RGB sync" refers to the separate rear RGB LED.

The footer's visible boundaries and touch hitboxes are identical: Shuffle
0–63, Previous 64–117, Play/Pause 118–201, Next 202–255, and More 256–319.
The headerless Now Playing artwork and queue card are informational. The tiny
upper-right `LINK`/`RETRY` status target opens the Actions screen; the footer
More target also opens or closes Actions.

### Backlight caution

GPIO 21 is configured as a 20 kHz PWM signal. It must drive a breakout's logic
backlight input or a suitable transistor/MOSFET stage. Do not connect a bare
backlight string directly to the GPIO; ESP32 pins are not LED power supplies.
Set `kBacklightInverted` when the external driver is active-low.

## Status LED

| Signal | ESP32 GPIO | Firmware symbol |
| --- | ---: | --- |
| Red (active-low) | 4 | `kStatusLedRed` |
| Green (active-low) | 16 | `kStatusLedGreen` |
| Blue (active-low) | 17 | `kStatusLedBlue` |

Connect each channel through suitable current limiting. The RGB LED is optional;
if it is not fitted, leave the channels unconnected.

## Optional hardware

The encoder, buttons, buzzer, and haptic output are disabled with pin `-1` in
the published profile. Adding them requires a dedicated adapter and pin
assignment. Do not reuse an assigned GPIO.

## Compile-time safety checks

The profile constructs one list of assigned pins and fails compilation when a
pin is duplicated. GPIO 36 and 39 are used by the touch controller on this
board; do not repurpose them. Consult the board schematic and Espressif
datasheet before changing any assigned pin.

## Bring-up order

1. With power disconnected, continuity-check ground and every signal.
2. Confirm no 3.3 V/GND short and confirm display supply requirements.
3. Connect USB only; verify the ESP32 enumerates and does not heat unexpectedly.
4. Flash firmware with the display disconnected if board identity is uncertain.
5. Connect the display and verify splash orientation/backlight.
6. Verify touch corners and each touch target, checking the 180-degree mapping.
7. Add the optional RGB LED last.

Record the board revision, display module marking, supply voltage, and any
profile changes in the [hardware smoke-test record](HARDWARE_SMOKE_TEST.md).
