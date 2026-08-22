#include "controls/input_manager.h"

#include <cstdlib>

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
#if defined(DESKWAVE_ESP32_D0WD_V3)
    initializeTouch();
#else
    pinMode(hardware::kEncoderA, INPUT_PULLUP);
    pinMode(hardware::kEncoderB, INPUT_PULLUP);
    pinMode(hardware::kEncoderSwitch, INPUT_PULLUP);
    pinMode(hardware::kLeftButton, INPUT_PULLUP);
    pinMode(hardware::kRightButton, INPUT_PULLUP);
    pinMode(hardware::kMenuButton, INPUT_PULLUP);
    encoder_.reset(digitalRead(hardware::kEncoderA) != LOW, digitalRead(hardware::kEncoderB) != LOW,
                   micros());
#endif
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
                                 const core::PhysicalControl control, const std::uint32_t nowMs) {
    const auto signal = tracker.update(pressed, nowMs);
    if (signal != core::ButtonSignal::None) {
        publish(control, gestureFor(signal));
    }
}

void InputManager::poll(const std::uint32_t nowMs, const std::uint32_t nowUs) {
#if defined(DESKWAVE_ESP32_D0WD_V3)
    (void)nowUs;
    pollTouch(nowMs);
#else
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
#endif
}

bool InputManager::factoryResetChordActive() const noexcept { return factoryResetChordActive_; }

void InputManager::setControlContext(const core::ControlContext context) noexcept {
#if defined(DESKWAVE_ESP32_D0WD_V3)
    controlContext_ = context;
#else
    (void)context;
#endif
}

#if defined(DESKWAVE_ESP32_D0WD_V3)

void InputManager::initializeTouch() {
    pinMode(hardware::kTouchCs, OUTPUT);
    pinMode(hardware::kTouchClk, OUTPUT);
    pinMode(hardware::kTouchDin, OUTPUT);
    pinMode(hardware::kTouchMiso, INPUT);
    pinMode(hardware::kTouchIrq, INPUT);
    digitalWrite(hardware::kTouchCs, HIGH);
    digitalWrite(hardware::kTouchClk, HIGH);
}

std::uint16_t InputManager::touchReadAdc(const std::uint8_t command) {
    digitalWrite(hardware::kTouchCs, LOW);
    delayMicroseconds(1);
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
        digitalWrite(hardware::kTouchClk, LOW);
        digitalWrite(hardware::kTouchDin, (command & (0x80U >> bit)) != 0 ? HIGH : LOW);
        delayMicroseconds(3);
        digitalWrite(hardware::kTouchClk, HIGH);
        delayMicroseconds(3);
    }
    digitalWrite(hardware::kTouchClk, LOW);
    delayMicroseconds(5);
    std::uint16_t value = 0;
    for (std::uint8_t bit = 0; bit < 13; ++bit) {
        digitalWrite(hardware::kTouchClk, LOW);
        delayMicroseconds(3);
        value = static_cast<std::uint16_t>((value << 1U) |
                                           (digitalRead(hardware::kTouchMiso) ? 1U : 0U));
        digitalWrite(hardware::kTouchClk, HIGH);
        delayMicroseconds(3);
    }
    digitalWrite(hardware::kTouchCs, HIGH);
    return value;
}

bool InputManager::readTouch(std::int16_t& x, std::int16_t& y) {
    (void)touchReadAdc(0xD0);
    const auto rawX = touchReadAdc(0xD0);
    const auto rawY = touchReadAdc(0x90);
    if (rawY <= 100 || rawY >= 4'080 || rawX <= 100 || rawX >= 4'000) {
        return false;
    }
    // The ESP32-D0WD-V3 panel is mounted 180 degrees opposite the touch overlay. The
    // panel rotation is therefore a swapped-axis transform with both axes
    // reversed, using the calibrated endpoints for this exact loom.
    x = static_cast<std::int16_t>(constrain(
        map(constrain(rawY, std::uint16_t(200), std::uint16_t(3'900)), 200, 3'900, 319, 0), 0,
        319));
    y = static_cast<std::int16_t>(constrain(
        map(constrain(rawX, std::uint16_t(200), std::uint16_t(3'900)), 200, 3'900, 239, 0), 0,
        239));
    return true;
}

core::PhysicalControl InputManager::touchControlAt(const std::int16_t x,
                                                   const std::int16_t y) const {
    // Keep the upper-right status target actionable even though the rest of the
    // header is informational. It is the only persistent navigation affordance
    // on the touch-only ESP32-D0WD-V3 profile.
    if (x >= 238 && y < 36) {
        return controlContext_ == core::ControlContext::Playback
                   ? core::PhysicalControl::MoreButton
                   : core::PhysicalControl::MenuButton;
    }
    if (controlContext_ == core::ControlContext::Actions) {
        if (y >= 82 && y <= 179) {
            if (x >= 12 && x <= 153) {
                return core::PhysicalControl::ShuffleButton;
            }
            if (x >= 166 && x <= 307) {
                return core::PhysicalControl::RepeatButton;
            }
        }
        if (y >= 195 && x >= 256) {
            return core::PhysicalControl::MoreButton;
        }
        return core::PhysicalControl::NoControl;
    }
    // Match the five full-height footer targets drawn by the now-playing UI.
    if (controlContext_ == core::ControlContext::Playback && y >= 190) {
        if (x < 64) {
            return core::PhysicalControl::ShuffleButton;
        }
        if (x < 118) {
            return core::PhysicalControl::LeftButton;
        }
        if (x < 202) {
            return core::PhysicalControl::EncoderButton;
        }
        if (x < 256) {
            return core::PhysicalControl::RightButton;
        }
        return core::PhysicalControl::MoreButton;
    }
    if (controlContext_ == core::ControlContext::Device && y >= 136 && y <= 205) {
        return core::PhysicalControl::EncoderButton;
    }
    if (controlContext_ == core::ControlContext::Settings && y >= 34 && y <= 211) {
        return core::PhysicalControl::EncoderButton;
    }
    return core::PhysicalControl::NoControl;
}

void InputManager::pollTouch(const std::uint32_t nowMs) {
    if (static_cast<std::uint32_t>(nowMs - lastTouchPollMs_) < 18) {
        return;
    }
    lastTouchPollMs_ = nowMs;

    std::int16_t x = 0;
    std::int16_t y = 0;
    const bool pressed = readTouch(x, y);
    if (pressed) {
        if (!touchActive_) {
            touchActive_ = true;
            touchStable_ = false;
            touchStableSamples_ = 1;
            touchMoved_ = false;
            touchLongEmitted_ = false;
            touchLastX_ = x;
            touchLastY_ = y;
            return;
        }
        if (!touchStable_) {
            if (std::abs(static_cast<int>(x) - touchLastX_) < 25 &&
                std::abs(static_cast<int>(y) - touchLastY_) < 25) {
                touchStableSamples_ = std::min<std::uint8_t>(10, touchStableSamples_ + 1);
            } else {
                touchStableSamples_ = 1;
            }
            touchLastX_ = x;
            touchLastY_ = y;
            if (touchStableSamples_ < 3) {
                return;
            }
            touchStable_ = true;
            touchStartedAtMs_ = nowMs;
            nextTouchRepeatAtMs_ = nowMs + 1'200;
            touchStartX_ = x;
            touchStartY_ = y;
            touchControl_ = touchControlAt(x, y);
        } else if (std::abs(static_cast<int>(x) - touchStartX_) > 24 ||
                   std::abs(static_cast<int>(y) - touchStartY_) > 24) {
            touchMoved_ = true;
        }
        touchLastX_ = x;
        touchLastY_ = y;
        if (!touchMoved_) {
            const auto age = static_cast<std::uint32_t>(nowMs - touchStartedAtMs_);
            if (!touchLongEmitted_ && age >= 700) {
                publish(touchControl_, core::Gesture::LongPress);
                touchLongEmitted_ = true;
            } else if (touchLongEmitted_ &&
                       static_cast<std::int32_t>(nowMs - nextTouchRepeatAtMs_) >= 0) {
                publish(touchControl_, core::Gesture::Repeat);
                nextTouchRepeatAtMs_ = nowMs + 160;
            }
        }
        return;
    }

    if (!touchActive_) {
        return;
    }
    if (!touchStable_) {
        touchActive_ = false;
        touchStableSamples_ = 0;
        return;
    }
    const auto duration = static_cast<std::uint32_t>(nowMs - touchStartedAtMs_);
    if (touchMoved_) {
        const auto deltaX = static_cast<int>(touchLastX_) - touchStartX_;
        const auto deltaY = static_cast<int>(touchLastY_) - touchStartY_;
        if (std::abs(deltaY) >= std::abs(deltaX) && std::abs(deltaY) >= 40) {
            publish(deltaY < 0 ? core::PhysicalControl::EncoderClockwise
                               : core::PhysicalControl::EncoderCounterClockwise,
                    core::Gesture::Rotate);
        } else if (std::abs(deltaX) >= 40) {
            publish(
                deltaX < 0 ? core::PhysicalControl::LeftButton : core::PhysicalControl::RightButton,
                core::Gesture::ShortPress);
        }
    } else if (!touchLongEmitted_) {
        const auto gesture = duration >= 700 ? core::Gesture::LongPress
                                             : core::Gesture::ShortPress;
        publish(touchControl_, gesture);
    }
    touchActive_ = false;
    touchStable_ = false;
    touchStableSamples_ = 0;
    touchMoved_ = false;
    touchLongEmitted_ = false;
}

#endif

}  // namespace deskwave::controls
