# Reference wiring

The default profile targets an **ESP32-S3-DevKitC-1-N8** and a 2.8-inch-class
SPI **ILI9341** panel. The controller uses 3.3 V logic. Check the exact display
breakout's regulator, backlight resistor/driver, and pin labels before wiring;
bare panels and 5 V-tolerant breakout boards are not electrically equivalent.

Disconnect USB and external power while changing wiring. All modules must share
ground. Never power the ESP32 simultaneously from unrelated supplies unless the
power design explicitly supports it.

## Display

| ILI9341 signal | ESP32-S3 GPIO | Firmware symbol | Notes |
| --- | ---: | --- | --- |
| SCLK / CLK | 12 | `kDisplaySclk` | SPI clock, 40 MHz write target |
| MOSI / SDI | 11 | `kDisplayMosi` | ESP32 to display data |
| MISO / SDO | 13 | `kDisplayMiso` | Display to ESP32; retain for readable/shared panels |
| CS | 10 | `kDisplayCs` | Active-low chip select |
| DC / RS | 9 | `kDisplayDc` | Data/command select |
| RST / RESET | 8 | `kDisplayReset` | Active-low panel reset |
| LED / BL | 14 | `kBacklight` | PWM control input, not an unbounded LED power feed |
| GND | GND | — | Required common ground |
| VCC | Per breakout | — | Use the voltage specified by the module vendor |

The physical panel is 240×320 and firmware rotates it to 320×240 landscape.
If colors are swapped, orientation is wrong, or the module inverts brightness,
change only the panel flags/rotation in
`firmware/include/config/hardware_config.h` and the display adapter—not UI
coordinates or application logic.

## Classic ESP32 Focus profile

The `esp32-focus` PlatformIO environment targets the locally verified
ESP32-D0WD-V3 board and its known-good Focus panel loom:

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

This profile has no encoder/button loom. Touching the footer activates the
previous, play/pause, next, and menu controls; vertical swipes act as encoder
turns, and horizontal swipes act as previous/next. The touch calibration is the
same 200–3900 endpoint mapping used by the verified Focus firmware.

The on-screen Shuffle and Repeat chips are direct touch targets. The top-right
status target opens or closes the Actions screen; the footer `MORE` target still
advances through the primary screens.

### Backlight caution

GPIO 14 is configured as a 20 kHz PWM signal. It must drive a breakout's logic
backlight input or a suitable transistor/MOSFET stage. Do not connect a bare
backlight string directly to the GPIO; ESP32 pins are not LED power supplies.
Set `kBacklightInverted` when the external driver is active-low.

## Rotary encoder

| Encoder signal | ESP32-S3 GPIO | Firmware symbol |
| --- | ---: | --- |
| A / CLK | 4 | `kEncoderA` |
| B / DT | 5 | `kEncoderB` |
| Push switch | 6 | `kEncoderSwitch` |
| Common | GND | — |

The inputs use internal pull-ups, so each contact closes to ground. If physical
rotation is reversed, swap A and B in the wiring or exchange the two constants
in the hardware profile; do not invert volume semantics in application code.

## Buttons

| Control | ESP32-S3 GPIO | Firmware symbol | Other terminal |
| --- | ---: | --- | --- |
| Left | 7 | `kLeftButton` | GND |
| Right | 15 | `kRightButton` | GND |
| Menu | 16 | `kMenuButton` | GND |

Buttons are active-low with internal pull-ups. External debounce components are
not required for the reference build because firmware applies stable-state
debounce, but long/noisy cable runs may benefit from hardware conditioning.

## Status LED

| Signal | ESP32-S3 GPIO | Firmware symbol |
| --- | ---: | --- |
| Status LED output | 17 | `kStatusLed` |

Connect GPIO 17 through an appropriate current-limiting resistor to an LED and
then ground (active-high reference). Connected is steady; reconnecting is a
slow pulse; a fatal state flashes rapidly. If no LED is fitted, leave the pin
unconnected.

## Optional hardware

Touch, buzzer, and haptic output are disabled with pin `-1` in the reference
profile. Adding them requires a dedicated adapter and pin assignment. Do not
reuse an assigned GPIO.

## Compile-time safety checks

The profile constructs one list of assigned pins and fails compilation when a
pin is duplicated. The reference ESP32-S3 profile also rejects:

- GPIO 0 and 3 (strap-sensitive defaults);
- GPIO 26–37 (integrated flash/PSRAM-sensitive range on common modules);
- GPIO 45 and 46 (strap/input limitations).

These conservative checks protect the reference board; a different module may
have additional restrictions. Consult its schematic and Espressif datasheet.

## Bring-up order

1. With power disconnected, continuity-check ground and every signal.
2. Confirm no 3.3 V/GND short and confirm display supply requirements.
3. Connect USB only; verify the ESP32 enumerates and does not heat unexpectedly.
4. Flash firmware with the display disconnected if board identity is uncertain.
5. Connect the display and verify splash orientation/backlight.
6. Add encoder, then buttons, checking each through the smoke test.
7. Add the optional LED last.

Record the board revision, display module marking, supply voltage, and any
profile changes in the [hardware smoke-test record](HARDWARE_SMOKE_TEST.md).
