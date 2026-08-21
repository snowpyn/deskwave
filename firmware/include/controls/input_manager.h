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

  private:
    static void taskEntry(void* context);
    void run();
    void publish(core::PhysicalControl control, core::Gesture gesture);
    void processButton(core::ButtonTracker& tracker, bool pressed,
                       core::PhysicalControl control, std::uint32_t nowMs);

    QueueHandle_t eventQueue_;
    core::EncoderTracker encoder_;
    core::ButtonTracker encoderButton_;
    core::ButtonTracker leftButton_;
    core::ButtonTracker rightButton_;
    core::ButtonTracker menuButton_;
    TaskHandle_t task_{nullptr};
    std::uint32_t lastQueueWarningMs_{0};
    volatile bool factoryResetChordActive_{false};
};

}  // namespace deskwave::controls
