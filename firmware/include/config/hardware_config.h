#pragma once

#include <array>
#include <cstdint>

#include "deskwave/core/pin_validation.h"

namespace deskwave::hardware {

// Change this file, not application logic, when adapting a board or wiring loom.
#if defined(DESKWAVE_FOCUS_CLASSIC)
// Locally verified ESP32-D0WD-V3 Focus board with the 320x240 ILI9341/XPT2046
// panel. The touch panel replaces the reference encoder and button loom.
inline constexpr char kBoardName[] = "ESP32 Dev Module / ESP32-D0WD-V3 (Focus)";
#else
// Reference target: ESP32-S3-DevKitC-1-N8 with a 2.8-inch SPI ILI9341 panel.
inline constexpr char kBoardName[] = "ESP32-S3-DevKitC-1-N8";
#endif
inline constexpr char kDisplayDriver[] = "ILI9341";
inline constexpr std::uint16_t kPanelWidth = 240;
inline constexpr std::uint16_t kPanelHeight = 320;
inline constexpr std::uint16_t kDisplayWidth = 320;
inline constexpr std::uint16_t kDisplayHeight = 240;
inline constexpr std::uint8_t kDisplayRotation = 1;

#if defined(DESKWAVE_FOCUS_CLASSIC)
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
inline constexpr int kStatusLed = 17;
inline constexpr int kBuzzer = -1;

inline constexpr std::array kAssignedPins{
    kDisplaySclk, kDisplayMosi, kDisplayMiso, kDisplayCs, kDisplayDc, kBacklight,
    kTouchCs,    kTouchIrq,    kTouchDin,    kTouchMiso, kTouchClk, kStatusLed,
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
inline constexpr int kStatusLed = 17;
inline constexpr int kBuzzer = -1;

inline constexpr std::array kAssignedPins{
    kDisplaySclk,  kDisplayMosi, kDisplayMiso, kDisplayCs, kDisplayDc,
    kDisplayReset, kBacklight,   kEncoderA,    kEncoderB,  kEncoderSwitch,
    kLeftButton,   kRightButton, kMenuButton,  kStatusLed,
};
#endif

static_assert(core::pinsUnique(kAssignedPins), "DeskWave GPIO assignments must be unique");
#if !defined(DESKWAVE_FOCUS_CLASSIC)
static_assert(core::pinsAvoidUnsafeEsp32S3Defaults(kAssignedPins),
              "Reference wiring uses an ESP32-S3 strapping or flash GPIO");
#endif

}  // namespace deskwave::hardware
