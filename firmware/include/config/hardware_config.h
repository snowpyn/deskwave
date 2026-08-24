#pragma once

#include <array>
#include <cstdint>

#include "deskwave/core/pin_validation.h"

namespace deskwave::hardware {

// Change this file, not application logic, when adapting a board or wiring loom.
#if defined(DESKWAVE_ESP32_D0WD_V3)
// Locally verified ESP32-D0WD-V3 Revision 3.1 board with the 320x240
// ILI9341/XPT2046 panel. The touch panel replaces the encoder and button loom.
inline constexpr char kBoardName[] = "ESP32 Dev Module / ESP32-D0WD-V3";
#else
// Legacy fallback for callers that compile the hardware adapter without the
// published ESP32-D0WD-V3 PlatformIO environment.
inline constexpr char kBoardName[] = "Unsupported hardware (use esp32-d0wd-v3)";
#endif
inline constexpr char kDisplayDriver[] = "ILI9341";
inline constexpr std::uint16_t kPanelWidth = 240;
inline constexpr std::uint16_t kPanelHeight = 320;
inline constexpr std::uint16_t kDisplayWidth = 320;
inline constexpr std::uint16_t kDisplayHeight = 240;
#if defined(DESKWAVE_ESP32_D0WD_V3)
// The touch transform is calibrated independently; rotation 1 is the required
// 180-degree flip for the rendered screen on the Revision 3.1 panel.
inline constexpr std::uint8_t kDisplayRotation = 1;
#else
inline constexpr std::uint8_t kDisplayRotation = 1;
#endif

#if defined(DESKWAVE_ESP32_D0WD_V3)
inline constexpr int kDisplaySclk = 14;
inline constexpr int kDisplayMosi = 13;
inline constexpr int kDisplayMiso = 12;
inline constexpr int kDisplayCs = 15;
inline constexpr int kDisplayDc = 2;
inline constexpr int kDisplayReset = -1;
inline constexpr int kBacklight = 21;
inline constexpr std::uint32_t kDisplayWriteHz = 26'000'000;
inline constexpr std::uint32_t kDisplayReadHz = 8'000'000;
inline constexpr bool kBacklightInverted = false;

inline constexpr int kEncoderA = -1;
inline constexpr int kEncoderB = -1;
inline constexpr int kEncoderSwitch = -1;
inline constexpr int kLeftButton = -1;
inline constexpr int kRightButton = -1;
inline constexpr int kMenuButton = -1;

inline constexpr int kTouchCs = 33;
inline constexpr int kTouchIrq = 36;
inline constexpr int kTouchDin = 32;
inline constexpr int kTouchMiso = 39;
inline constexpr int kTouchClk = 25;
inline constexpr bool kHasRgbStatusLed = true;
// The locally verified Revision 3.1 unit has the RGB package's red/blue dies
// reversed relative to the commonly published CYD mapping. Live channel testing
// confirms that GPIO 17 is red, GPIO 16 is green, and GPIO 4 is blue.
inline constexpr int kStatusLedRed = 17;
inline constexpr int kStatusLedGreen = 16;
inline constexpr int kStatusLedBlue = 4;
// Per-channel white balance for the on-board common-anode RGB LED. The green
// and blue dies are brighter than red at the same PWM duty on this board.
inline constexpr std::uint8_t kStatusLedRedCalibration = 255;
inline constexpr std::uint8_t kStatusLedGreenCalibration = 176;
inline constexpr std::uint8_t kStatusLedBlueCalibration = 240;
inline constexpr int kStatusLed = 17;
inline constexpr int kBuzzer = -1;

inline constexpr std::array kAssignedPins{
    kDisplaySclk, kDisplayMosi,  kDisplayMiso,    kDisplayCs,     kDisplayDc,
    kBacklight,   kTouchCs,      kTouchIrq,       kTouchDin,      kTouchMiso,
    kTouchClk,    kStatusLedRed, kStatusLedGreen, kStatusLedBlue,
};
#else
inline constexpr int kDisplaySclk = 12;
inline constexpr int kDisplayMosi = 11;
inline constexpr int kDisplayMiso = 13;
inline constexpr int kDisplayCs = 10;
inline constexpr int kDisplayDc = 9;
inline constexpr int kDisplayReset = 8;
inline constexpr int kBacklight = 14;
inline constexpr std::uint32_t kDisplayWriteHz = 40'000'000;
inline constexpr std::uint32_t kDisplayReadHz = 16'000'000;
inline constexpr bool kBacklightInverted = false;

inline constexpr int kEncoderA = 4;
inline constexpr int kEncoderB = 5;
inline constexpr int kEncoderSwitch = 6;
inline constexpr int kLeftButton = 7;
inline constexpr int kRightButton = 15;
inline constexpr int kMenuButton = 16;

inline constexpr int kTouchCs = -1;
inline constexpr int kTouchIrq = -1;
inline constexpr int kTouchDin = -1;
inline constexpr int kTouchMiso = -1;
inline constexpr int kTouchClk = -1;
inline constexpr bool kHasRgbStatusLed = false;
inline constexpr int kStatusLedRed = -1;
inline constexpr int kStatusLedGreen = -1;
inline constexpr int kStatusLedBlue = 17;
inline constexpr std::uint8_t kStatusLedRedCalibration = 255;
inline constexpr std::uint8_t kStatusLedGreenCalibration = 255;
inline constexpr std::uint8_t kStatusLedBlueCalibration = 255;
inline constexpr int kStatusLed = 17;
inline constexpr int kBuzzer = -1;

inline constexpr std::array kAssignedPins{
    kDisplaySclk,  kDisplayMosi, kDisplayMiso, kDisplayCs, kDisplayDc,
    kDisplayReset, kBacklight,   kEncoderA,    kEncoderB,  kEncoderSwitch,
    kLeftButton,   kRightButton, kMenuButton,  kStatusLed,
};
#endif

static_assert(core::pinsUnique(kAssignedPins), "DeskWave GPIO assignments must be unique");
#if !defined(DESKWAVE_ESP32_D0WD_V3)
static_assert(core::pinsAvoidUnsafeEsp32S3Defaults(kAssignedPins),
              "Legacy fallback wiring uses an unsafe ESP32 strapping or flash GPIO");
#endif

}  // namespace deskwave::hardware
