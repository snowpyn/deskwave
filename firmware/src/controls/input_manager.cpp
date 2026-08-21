#include "controls/input_manager.h"

#include "config/hardware_config.h"
#include "system/logging.h"

namespace deskwave::controls {
namespace {

core::Gesture gestureFor(const core::ButtonSignal signal) {
    switch (signal) {
        case core::ButtonSignal::ShortPress:
            return core::Gesture::ShortPress;
        case core::ButtonSignal::LongPress:
            return core::Gesture::LongPress;
        case core::ButtonSignal::Repeat:
            return core::Gesture::Repeat;
        case core::ButtonSignal::None:
            return core::Gesture::ShortPress;
    }
    return core::Gesture::ShortPress;
}

}  // namespace

InputManager::InputManager(const QueueHandle_t eventQueue)
    : eventQueue_(eventQueue),
      encoder_(180),
      encoderButton_(core::ButtonTiming{25, 700, 450, 120, false}),
      leftButton_(core::ButtonTiming{25, 700, 300, 160, true}),
      rightButton_(core::ButtonTiming{25, 700, 300, 160, true}),
      menuButton_(core::ButtonTiming{25, 900, 450, 120, false}) {}

bool InputManager::begin() {
    if (task_ != nullptr) {
        return true;
    }
    pinMode(hardware::kEncoderA, INPUT_PULLUP);
    pinMode(hardware::kEncoderB, INPUT_PULLUP);
    pinMode(hardware::kEncoderSwitch, INPUT_PULLUP);
    pinMode(hardware::kLeftButton, INPUT_PULLUP);
    pinMode(hardware::kRightButton, INPUT_PULLUP);
    pinMode(hardware::kMenuButton, INPUT_PULLUP);
    encoder_.reset(digitalRead(hardware::kEncoderA) != LOW,
                   digitalRead(hardware::kEncoderB) != LOW, micros());
    return xTaskCreatePinnedToCore(taskEntry, "deskwave-input", 3'072, this, 3, &task_, 1) ==
           pdPASS;
}

void InputManager::taskEntry(void* context) {
    static_cast<InputManager*>(context)->run();
    vTaskDelete(nullptr);
}

void InputManager::run() {
    while (true) {
        poll(millis(), micros());
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void InputManager::publish(const core::PhysicalControl control, const core::Gesture gesture) {
    const InputEvent event{control, gesture};
    if (xQueueSend(eventQueue_, &event, 0) != pdTRUE) {
        const auto now = millis();
        if (static_cast<std::uint32_t>(now - lastQueueWarningMs_) >= 2'000) {
            DW_LOG_WARN("input", "Input event queue is full");
            lastQueueWarningMs_ = now;
        }
    }
}

void InputManager::processButton(core::ButtonTracker& tracker, const bool pressed,
                                 const core::PhysicalControl control,
                                 const std::uint32_t nowMs) {
    const auto signal = tracker.update(pressed, nowMs);
    if (signal != core::ButtonSignal::None) {
        publish(control, gestureFor(signal));
    }
}

void InputManager::poll(const std::uint32_t nowMs, const std::uint32_t nowUs) {
    const auto encoderStep = encoder_.update(digitalRead(hardware::kEncoderA) != LOW,
                                             digitalRead(hardware::kEncoderB) != LOW, nowUs);
    if (encoderStep > 0) {
        publish(core::PhysicalControl::EncoderClockwise, core::Gesture::Rotate);
    } else if (encoderStep < 0) {
        publish(core::PhysicalControl::EncoderCounterClockwise, core::Gesture::Rotate);
    }
    processButton(encoderButton_, digitalRead(hardware::kEncoderSwitch) == LOW,
                  core::PhysicalControl::EncoderButton, nowMs);
    processButton(leftButton_, digitalRead(hardware::kLeftButton) == LOW,
                  core::PhysicalControl::LeftButton, nowMs);
    processButton(rightButton_, digitalRead(hardware::kRightButton) == LOW,
                  core::PhysicalControl::RightButton, nowMs);
    processButton(menuButton_, digitalRead(hardware::kMenuButton) == LOW,
                  core::PhysicalControl::MenuButton, nowMs);
    factoryResetChordActive_ =
        leftButton_.isPressed() && rightButton_.isPressed() && menuButton_.isPressed();
}

bool InputManager::factoryResetChordActive() const noexcept {
    return factoryResetChordActive_;
}

}  // namespace deskwave::controls
