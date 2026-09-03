#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace deskwave::core {

enum class ButtonSignal : std::uint8_t { None, ShortPress, LongPress, Repeat };

struct ButtonTiming {
    std::uint32_t debounceMs{25};
    std::uint32_t longPressMs{700};
    std::uint32_t repeatDelayMs{450};
    std::uint32_t repeatIntervalMs{120};
    bool repeatEnabled{false};
};

class ButtonTracker {
   public:
    explicit ButtonTracker(ButtonTiming timing = {});
    [[nodiscard]] ButtonSignal update(bool pressed, std::uint32_t nowMs) noexcept;
    [[nodiscard]] bool isPressed() const noexcept;

   private:
    ButtonTiming timing_;
    bool rawPressed_{false};
    bool stablePressed_{false};
    bool longEmitted_{false};
    std::uint32_t rawChangedAt_{0};
    std::uint32_t pressedAt_{0};
    std::uint32_t nextRepeatAt_{0};
};

class EncoderTracker {
   public:
    explicit EncoderTracker(std::uint32_t minimumEdgeIntervalUs = 180);
    [[nodiscard]] std::int8_t update(bool channelA, bool channelB, std::uint32_t nowUs) noexcept;
    void reset(bool channelA, bool channelB, std::uint32_t nowUs) noexcept;

   private:
    std::uint8_t previous_{0};
    std::int8_t accumulator_{0};
    std::uint32_t lastEdgeAt_{0};
    std::uint32_t minimumEdgeIntervalUs_;
    bool initialized_{false};
};

enum class ControlContext : std::uint8_t { Playback, Actions, Device, Settings };
enum class PhysicalControl : std::uint8_t {
    NoControl,
    EncoderClockwise,
    EncoderCounterClockwise,
    EncoderButton,
    LeftButton,
    RightButton,
    MenuButton,
    ShuffleButton,
    RepeatButton,
    MoreButton,
};
enum class Gesture : std::uint8_t { Rotate, ShortPress, LongPress, Repeat };
enum class ControlCommand : std::uint8_t {
    None,
    VolumeUp,
    VolumeDown,
    TogglePlayback,
    Mute,
    Previous,
    Next,
    SeekBackward,
    SeekForward,
    NextScreen,
    OpenActions,
    ShuffleToggle,
    CycleRepeat,
    BrightnessDown,
    BrightnessUp,
    PreviousSetting,
    NextSetting,
    ActivateSetting,
    PreviousPlayer,
    NextPlayer,
    SelectPlayer,
    RequestFactoryReset,
};

struct ControlBinding {
    ControlContext context;
    PhysicalControl control;
    Gesture gesture;
    ControlCommand command;
};

class ControlMapper {
   public:
    static constexpr std::size_t kMaxBindings = 40;

    ControlMapper();
    [[nodiscard]] ControlCommand map(ControlContext context, PhysicalControl control,
                                     Gesture gesture) const noexcept;
    [[nodiscard]] bool setBinding(std::size_t index, ControlBinding binding) noexcept;

   private:
    std::array<ControlBinding, kMaxBindings> bindings_{};
    std::size_t bindingCount_{0};
};

}  // namespace deskwave::core
