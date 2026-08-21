#include "deskwave/core/input_logic.h"

#include <limits>

namespace deskwave::core {
namespace {

constexpr bool elapsed(const std::uint32_t now, const std::uint32_t then,
                       const std::uint32_t interval) noexcept {
    return static_cast<std::uint32_t>(now - then) >= interval;
}

constexpr bool deadlineReached(const std::uint32_t now, const std::uint32_t deadline) noexcept {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

}  // namespace

ButtonTracker::ButtonTracker(const ButtonTiming timing) : timing_(timing) {}

bool ButtonTracker::isPressed() const noexcept {
    return stablePressed_;
}

ButtonSignal ButtonTracker::update(const bool pressed, const std::uint32_t nowMs) noexcept {
    if (pressed != rawPressed_) {
        rawPressed_ = pressed;
        rawChangedAt_ = nowMs;
    }
    if (rawPressed_ != stablePressed_ && elapsed(nowMs, rawChangedAt_, timing_.debounceMs)) {
        stablePressed_ = rawPressed_;
        if (stablePressed_) {
            pressedAt_ = nowMs;
            longEmitted_ = false;
            nextRepeatAt_ = nowMs + timing_.longPressMs + timing_.repeatDelayMs;
        } else if (!longEmitted_) {
            return ButtonSignal::ShortPress;
        }
    }
    if (!stablePressed_) {
        return ButtonSignal::None;
    }
    if (!longEmitted_ && elapsed(nowMs, pressedAt_, timing_.longPressMs)) {
        longEmitted_ = true;
        return ButtonSignal::LongPress;
    }
    if (longEmitted_ && timing_.repeatEnabled && deadlineReached(nowMs, nextRepeatAt_)) {
        nextRepeatAt_ = nowMs + timing_.repeatIntervalMs;
        return ButtonSignal::Repeat;
    }
    return ButtonSignal::None;
}

EncoderTracker::EncoderTracker(const std::uint32_t minimumEdgeIntervalUs)
    : minimumEdgeIntervalUs_(minimumEdgeIntervalUs) {}

void EncoderTracker::reset(const bool channelA, const bool channelB,
                           const std::uint32_t nowUs) noexcept {
    previous_ = static_cast<std::uint8_t>((channelA ? 2U : 0U) | (channelB ? 1U : 0U));
    accumulator_ = 0;
    lastEdgeAt_ = nowUs;
    initialized_ = true;
}

std::int8_t EncoderTracker::update(const bool channelA, const bool channelB,
                                   const std::uint32_t nowUs) noexcept {
    const auto current =
        static_cast<std::uint8_t>((channelA ? 2U : 0U) | (channelB ? 1U : 0U));
    if (!initialized_) {
        reset(channelA, channelB, nowUs);
        return 0;
    }
    if (current == previous_) {
        return 0;
    }
    if (!elapsed(nowUs, lastEdgeAt_, minimumEdgeIntervalUs_)) {
        return 0;
    }
    constexpr std::array<std::int8_t, 16> transitions{
        0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0,
    };
    const auto index = static_cast<std::size_t>((previous_ << 2U) | current);
    accumulator_ = static_cast<std::int8_t>(accumulator_ + transitions[index]);
    previous_ = current;
    lastEdgeAt_ = nowUs;
    if (accumulator_ >= 4) {
        accumulator_ = 0;
        return 1;
    }
    if (accumulator_ <= -4) {
        accumulator_ = 0;
        return -1;
    }
    return 0;
}

ControlMapper::ControlMapper() {
    constexpr std::array defaults{
        ControlBinding{ControlContext::Playback, PhysicalControl::EncoderClockwise,
                       Gesture::Rotate, ControlCommand::VolumeUp},
        ControlBinding{ControlContext::Playback, PhysicalControl::EncoderCounterClockwise,
                       Gesture::Rotate, ControlCommand::VolumeDown},
        ControlBinding{ControlContext::Playback, PhysicalControl::EncoderButton,
                       Gesture::ShortPress, ControlCommand::TogglePlayback},
        ControlBinding{ControlContext::Playback, PhysicalControl::EncoderButton,
                       Gesture::LongPress, ControlCommand::Mute},
        ControlBinding{ControlContext::Playback, PhysicalControl::LeftButton,
                       Gesture::ShortPress, ControlCommand::Previous},
        ControlBinding{ControlContext::Playback, PhysicalControl::LeftButton,
                       Gesture::LongPress, ControlCommand::SeekBackward},
        ControlBinding{ControlContext::Playback, PhysicalControl::LeftButton, Gesture::Repeat,
                       ControlCommand::SeekBackward},
        ControlBinding{ControlContext::Playback, PhysicalControl::RightButton,
                       Gesture::ShortPress, ControlCommand::Next},
        ControlBinding{ControlContext::Playback, PhysicalControl::RightButton,
                       Gesture::LongPress, ControlCommand::SeekForward},
        ControlBinding{ControlContext::Playback, PhysicalControl::RightButton, Gesture::Repeat,
                       ControlCommand::SeekForward},
        ControlBinding{ControlContext::Actions, PhysicalControl::LeftButton,
                       Gesture::ShortPress, ControlCommand::ShuffleToggle},
        ControlBinding{ControlContext::Actions, PhysicalControl::RightButton,
                       Gesture::ShortPress, ControlCommand::CycleRepeat},
        ControlBinding{ControlContext::Settings, PhysicalControl::LeftButton,
                       Gesture::ShortPress, ControlCommand::BrightnessDown},
        ControlBinding{ControlContext::Settings, PhysicalControl::RightButton,
                       Gesture::ShortPress, ControlCommand::BrightnessUp},
        ControlBinding{ControlContext::Settings, PhysicalControl::LeftButton, Gesture::Repeat,
                       ControlCommand::BrightnessDown},
        ControlBinding{ControlContext::Settings, PhysicalControl::RightButton, Gesture::Repeat,
                       ControlCommand::BrightnessUp},
        ControlBinding{ControlContext::Settings, PhysicalControl::EncoderCounterClockwise,
                       Gesture::Rotate, ControlCommand::PreviousSetting},
        ControlBinding{ControlContext::Settings, PhysicalControl::EncoderClockwise,
                       Gesture::Rotate, ControlCommand::NextSetting},
        ControlBinding{ControlContext::Settings, PhysicalControl::EncoderButton,
                       Gesture::ShortPress, ControlCommand::ActivateSetting},
        ControlBinding{ControlContext::Settings, PhysicalControl::EncoderButton,
                       Gesture::LongPress, ControlCommand::RequestFactoryReset},
        ControlBinding{ControlContext::Device, PhysicalControl::EncoderCounterClockwise,
                       Gesture::Rotate, ControlCommand::PreviousPlayer},
        ControlBinding{ControlContext::Device, PhysicalControl::EncoderClockwise,
                       Gesture::Rotate, ControlCommand::NextPlayer},
        ControlBinding{ControlContext::Device, PhysicalControl::EncoderButton,
                       Gesture::ShortPress, ControlCommand::SelectPlayer},
        ControlBinding{ControlContext::Playback, PhysicalControl::MenuButton,
                       Gesture::ShortPress, ControlCommand::NextScreen},
        ControlBinding{ControlContext::Actions, PhysicalControl::MenuButton,
                       Gesture::ShortPress, ControlCommand::NextScreen},
        ControlBinding{ControlContext::Device, PhysicalControl::MenuButton,
                       Gesture::ShortPress, ControlCommand::NextScreen},
        ControlBinding{ControlContext::Settings, PhysicalControl::MenuButton,
                       Gesture::ShortPress, ControlCommand::NextScreen},
        ControlBinding{ControlContext::Playback, PhysicalControl::MenuButton,
                       Gesture::LongPress, ControlCommand::OpenActions},
        ControlBinding{ControlContext::Actions, PhysicalControl::MenuButton, Gesture::LongPress,
                       ControlCommand::OpenActions},
        ControlBinding{ControlContext::Device, PhysicalControl::MenuButton, Gesture::LongPress,
                       ControlCommand::OpenActions},
        ControlBinding{ControlContext::Settings, PhysicalControl::MenuButton,
                       Gesture::LongPress, ControlCommand::OpenActions},
    };
    bindingCount_ = defaults.size();
    for (std::size_t index = 0; index < defaults.size(); ++index) {
        bindings_[index] = defaults[index];
    }
}

ControlCommand ControlMapper::map(const ControlContext context, const PhysicalControl control,
                                  const Gesture gesture) const noexcept {
    for (std::size_t index = 0; index < bindingCount_; ++index) {
        const auto& binding = bindings_[index];
        if (binding.context == context && binding.control == control &&
            binding.gesture == gesture) {
            return binding.command;
        }
    }
    return ControlCommand::None;
}

bool ControlMapper::setBinding(const std::size_t index, const ControlBinding binding) noexcept {
    if (index >= kMaxBindings) {
        return false;
    }
    bindings_[index] = binding;
    if (index >= bindingCount_) {
        bindingCount_ = index + 1;
    }
    return true;
}

}  // namespace deskwave::core
