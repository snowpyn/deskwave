#pragma once

#include <array>
#include <cstdint>

#include "deskwave/core/pin_validation.h"

namespace deskwave::hardware {

// Reference target: ESP32-S3-DevKitC-1-N8 with a 2.8-inch SPI ILI9341 panel.
// Change this file, not application logic, when adapting a board or wiring loom.
inline constexpr char kBoardName[] = "ESP32-S3-DevKitC-1-N8";
inline constexpr char kDisplayDriver[] = "ILI9341";
inline constexpr std::uint16_t kDisplayWidth = 320;
inline constexpr std::uint16_t kDisplayHeight = 240;
inline constexpr std::uint8_t kDisplayRotation = 1;

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
inline constexpr int kStatusLed = 17;
inline constexpr int kBuzzer = -1;

inline constexpr std::array kAssignedPins{
    kDisplaySclk,  kDisplayMosi,   kDisplayMiso, kDisplayCs,  kDisplayDc,
    kDisplayReset, kBacklight,     kEncoderA,    kEncoderB,   kEncoderSwitch,
    kLeftButton,   kRightButton,   kMenuButton,  kStatusLed,
};

static_assert(core::pinsUnique(kAssignedPins), "DeskWave GPIO assignments must be unique");
static_assert(core::pinsAvoidUnsafeEsp32S3Defaults(kAssignedPins),
              "Reference wiring uses an ESP32-S3 strapping or flash GPIO");

}  // namespace deskwave::hardware
