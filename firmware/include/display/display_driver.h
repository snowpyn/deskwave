#pragma once

#include <LittleFS.h>

#include <LovyanGFX.hpp>
#include <cstdint>

namespace deskwave::display {

class DisplayDriver final : public lgfx::LGFX_Device {
   public:
    DisplayDriver();
    [[nodiscard]] bool initialize(std::uint8_t brightness);

   private:
    lgfx::Panel_ILI9341 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM backlight_;
};

}  // namespace deskwave::display
