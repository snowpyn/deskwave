#pragma once

#include <Arduino.h>

#include "deskwave/core/input_logic.h"

namespace deskwave::controls {

struct InputEvent {
    core::PhysicalControl control{core::PhysicalControl::EncoderButton};
    core::Gesture gesture{core::Gesture::ShortPress};
};

class InputManager {
   public:
    explicit InputManager(QueueHandle_t eventQueue);
    [[nodiscard]] bool begin();
    void poll(std::uint32_t nowMs, std::uint32_t nowUs);
    [[nodiscard]] bool factoryResetChordActive() const noexcept;
    void setControlContext(core::ControlContext context) noexcept;

   private:
    static void taskEntry(void* context);
    void run();
    void publish(core::PhysicalControl control, core::Gesture gesture);
    void processButton(core::ButtonTracker& tracker, bool pressed, core::PhysicalControl control,
                       std::uint32_t nowMs);
#if defined(DESKWAVE_ESP32_D0WD_V3)
    void initializeTouch();
    [[nodiscard]] bool readTouch(std::int16_t& x, std::int16_t& y);
    [[nodiscard]] std::uint16_t touchReadAdc(std::uint8_t command);
    void pollTouch(std::uint32_t nowMs);
    [[nodiscard]] core::PhysicalControl touchControlAt(std::int16_t x, std::int16_t y) const;
#endif

    QueueHandle_t eventQueue_;
    core::EncoderTracker encoder_;
    core::ButtonTracker encoderButton_;
    core::ButtonTracker leftButton_;
    core::ButtonTracker rightButton_;
    core::ButtonTracker menuButton_;
    TaskHandle_t task_{nullptr};
    std::uint32_t lastQueueWarningMs_{0};
    volatile bool factoryResetChordActive_{false};
#if defined(DESKWAVE_ESP32_D0WD_V3)
    core::PhysicalControl touchControl_{core::PhysicalControl::EncoderButton};
    std::uint32_t lastTouchPollMs_{0};
    std::uint32_t touchStartedAtMs_{0};
    std::uint32_t nextTouchRepeatAtMs_{0};
    std::int16_t touchStartX_{0};
    std::int16_t touchStartY_{0};
    std::int16_t touchLastX_{0};
    std::int16_t touchLastY_{0};
    std::uint8_t touchStableSamples_{0};
    bool touchActive_{false};
    bool touchStable_{false};
    bool touchMoved_{false};
    bool touchLongEmitted_{false};
    volatile core::ControlContext controlContext_{core::ControlContext::Playback};
#endif
};

}  // namespace deskwave::controls
