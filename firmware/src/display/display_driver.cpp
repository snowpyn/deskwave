#include "display/display_driver.h"

#include "config/hardware_config.h"

namespace deskwave::display {

DisplayDriver::DisplayDriver() {
    {
        auto configuration = bus_.config();
        configuration.spi_host = SPI2_HOST;
        configuration.spi_mode = 0;
        configuration.freq_write = hardware::kDisplayWriteHz;
        configuration.freq_read = hardware::kDisplayReadHz;
        configuration.spi_3wire = false;
        configuration.use_lock = true;
        configuration.dma_channel = SPI_DMA_CH_AUTO;
        configuration.pin_sclk = hardware::kDisplaySclk;
        configuration.pin_mosi = hardware::kDisplayMosi;
        configuration.pin_miso = hardware::kDisplayMiso;
        configuration.pin_dc = hardware::kDisplayDc;
        bus_.config(configuration);
        panel_.setBus(&bus_);
    }
    {
        auto configuration = panel_.config();
        configuration.pin_cs = hardware::kDisplayCs;
        configuration.pin_rst = hardware::kDisplayReset;
        configuration.pin_busy = -1;
        configuration.panel_width = hardware::kPanelWidth;
        configuration.panel_height = hardware::kPanelHeight;
        configuration.offset_x = 0;
        configuration.offset_y = 0;
        configuration.offset_rotation = 0;
        configuration.readable = true;
        configuration.invert = false;
        configuration.rgb_order = false;
        configuration.dlen_16bit = false;
        configuration.bus_shared = false;
        panel_.config(configuration);
    }
    {
        auto configuration = backlight_.config();
        configuration.pin_bl = hardware::kBacklight;
        configuration.invert = hardware::kBacklightInverted;
        configuration.freq = 20'000;
        configuration.pwm_channel = 7;
        backlight_.config(configuration);
        panel_.setLight(&backlight_);
    }
    setPanel(&panel_);
}

bool DisplayDriver::initialize(const std::uint8_t brightness) {
    if (!LGFX_Device::begin()) {
        return false;
    }
    setRotation(hardware::kDisplayRotation);
    setColorDepth(16);
    setBrightness(brightness);
    setTextWrap(false);
    return width() == hardware::kDisplayWidth && height() == hardware::kDisplayHeight;
}

}  // namespace deskwave::display
