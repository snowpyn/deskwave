#include <unity.h>

#include <array>

#include "deskwave/core/application_state.h"
#include "deskwave/core/backoff.h"
#include "deskwave/core/input_logic.h"
#include "deskwave/core/pin_validation.h"
#include "deskwave/core/progress.h"

using namespace deskwave::core;

void test_state_machine_happy_path_and_recovery() {
    StateMachine machine;
    TEST_ASSERT_EQUAL(SystemState::Boot, machine.state());
    TEST_ASSERT_TRUE(machine.transition(StateEvent::BootWithCredentials));
    TEST_ASSERT_EQUAL(SystemState::ConnectingWifi, machine.state());
    TEST_ASSERT_TRUE(machine.transition(StateEvent::WifiConnected));
    TEST_ASSERT_TRUE(machine.transition(StateEvent::HostDiscovered));
    TEST_ASSERT_TRUE(machine.transition(StateEvent::HostConnected));
    TEST_ASSERT_TRUE(machine.hostConnected());
    TEST_ASSERT_TRUE(machine.transition(StateEvent::PlaybackStarted));
    TEST_ASSERT_EQUAL(SystemState::Playing, machine.state());
    TEST_ASSERT_TRUE(machine.transition(StateEvent::HostDisconnected));
    TEST_ASSERT_EQUAL(SystemState::DiscoveringHost, machine.state());
    TEST_ASSERT_TRUE(machine.transition(StateEvent::HostConnected));
    TEST_ASSERT_TRUE(machine.hostConnected());
    TEST_ASSERT_TRUE(machine.transition(StateEvent::HostDisconnected));
    TEST_ASSERT_EQUAL(SystemState::DiscoveringHost, machine.state());
    TEST_ASSERT_TRUE(machine.transition(StateEvent::WifiLost));
    TEST_ASSERT_EQUAL(SystemState::Offline, machine.state());
    TEST_ASSERT_TRUE(machine.transition(StateEvent::Retry));
    TEST_ASSERT_EQUAL(SystemState::ConnectingWifi, machine.state());
}

void test_state_machine_rejects_illegal_transition() {
    StateMachine machine;
    TEST_ASSERT_FALSE(machine.transition(StateEvent::HostConnected));
    TEST_ASSERT_EQUAL(SystemState::Boot, machine.state());
}

void test_button_debounce_short_and_long_without_duplicates() {
    ButtonTracker button(ButtonTiming{25, 700, 100, 50, true});
    TEST_ASSERT_EQUAL(ButtonSignal::None, button.update(true, 10));
    TEST_ASSERT_EQUAL(ButtonSignal::None, button.update(false, 20));
    TEST_ASSERT_EQUAL(ButtonSignal::None, button.update(true, 30));
    TEST_ASSERT_EQUAL(ButtonSignal::None, button.update(true, 55));
    TEST_ASSERT_TRUE(button.isPressed());
    TEST_ASSERT_EQUAL(ButtonSignal::None, button.update(false, 100));
    TEST_ASSERT_EQUAL(ButtonSignal::ShortPress, button.update(false, 125));
    TEST_ASSERT_EQUAL(ButtonSignal::None, button.update(false, 126));

    TEST_ASSERT_EQUAL(ButtonSignal::None, button.update(true, 200));
    TEST_ASSERT_EQUAL(ButtonSignal::None, button.update(true, 225));
    TEST_ASSERT_EQUAL(ButtonSignal::LongPress, button.update(true, 925));
    TEST_ASSERT_EQUAL(ButtonSignal::None, button.update(true, 1000));
    TEST_ASSERT_EQUAL(ButtonSignal::Repeat, button.update(true, 1025));
    TEST_ASSERT_EQUAL(ButtonSignal::None, button.update(false, 1030));
    TEST_ASSERT_EQUAL(ButtonSignal::None, button.update(false, 1055));
}

void test_encoder_filters_edges_and_emits_one_detent() {
    EncoderTracker encoder(100);
    TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, false, 0));
    TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, true, 200));
    TEST_ASSERT_EQUAL_INT8(0, encoder.update(true, true, 400));
    TEST_ASSERT_EQUAL_INT8(0, encoder.update(true, false, 600));
    TEST_ASSERT_EQUAL_INT8(-1, encoder.update(false, false, 800));
    TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, true, 850));
}

void test_control_mapper_changes_with_context() {
    ControlMapper mapper;
    TEST_ASSERT_EQUAL(
        ControlCommand::Previous,
        mapper.map(ControlContext::Playback, PhysicalControl::LeftButton, Gesture::ShortPress));
    TEST_ASSERT_EQUAL(
        ControlCommand::ShuffleToggle,
        mapper.map(ControlContext::Actions, PhysicalControl::LeftButton, Gesture::ShortPress));
    TEST_ASSERT_EQUAL(
        ControlCommand::ShuffleToggle,
        mapper.map(ControlContext::Playback, PhysicalControl::ShuffleButton, Gesture::ShortPress));
    TEST_ASSERT_EQUAL(
        ControlCommand::CycleRepeat,
        mapper.map(ControlContext::Playback, PhysicalControl::RepeatButton, Gesture::ShortPress));
    TEST_ASSERT_EQUAL(
        ControlCommand::BrightnessDown,
        mapper.map(ControlContext::Settings, PhysicalControl::LeftButton, Gesture::ShortPress));
    TEST_ASSERT_EQUAL(
        ControlCommand::NextSetting,
        mapper.map(ControlContext::Settings, PhysicalControl::EncoderClockwise, Gesture::Rotate));
    TEST_ASSERT_EQUAL(
        ControlCommand::SelectPlayer,
        mapper.map(ControlContext::Device, PhysicalControl::EncoderButton, Gesture::ShortPress));
    TEST_ASSERT_EQUAL(ControlCommand::PreviousPlayer,
                      mapper.map(ControlContext::Device, PhysicalControl::EncoderCounterClockwise,
                                 Gesture::Rotate));
}

void test_progress_extrapolates_only_while_playing_and_clamps() {
    ProgressClock progress;
    progress.synchronize(1'000, 5'000, true, 100);
    TEST_ASSERT_EQUAL_UINT64(2'000, progress.position(1'100));
    progress.seek(10'000, 1'100);
    TEST_ASSERT_EQUAL_UINT64(5'000, progress.position(1'200));
    progress.synchronize(2'000, 5'000, false, 2'000);
    TEST_ASSERT_EQUAL_UINT64(2'000, progress.position(4'000));
    progress.seek(-3'000, 4'000);
    TEST_ASSERT_EQUAL_UINT64(0, progress.position(4'000));
}

void test_backoff_is_bounded_and_resettable() {
    ReconnectBackoff backoff(100, 1'000);
    TEST_ASSERT_EQUAL_UINT32(100, backoff.next(0));
    TEST_ASSERT_EQUAL_UINT32(200, backoff.next(0));
    for (int count = 0; count < 20; ++count) {
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(1'000, backoff.next(999));
    }
    backoff.reset();
    TEST_ASSERT_EQUAL_UINT32(100, backoff.next(0));
}

void test_pin_validation_detects_conflicts_and_unsafe_defaults() {
    constexpr std::array safePins{4, 5, 6, 7, -1};
    constexpr std::array duplicatePins{4, 5, 4};
    constexpr std::array unsafePins{4, 45};
    TEST_ASSERT_TRUE(pinsUnique(safePins));
    TEST_ASSERT_FALSE(pinsUnique(duplicatePins));
    TEST_ASSERT_TRUE(pinsAvoidUnsafeEsp32S3Defaults(safePins));
    TEST_ASSERT_FALSE(pinsAvoidUnsafeEsp32S3Defaults(unsafePins));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_state_machine_happy_path_and_recovery);
    RUN_TEST(test_state_machine_rejects_illegal_transition);
    RUN_TEST(test_button_debounce_short_and_long_without_duplicates);
    RUN_TEST(test_encoder_filters_edges_and_emits_one_detent);
    RUN_TEST(test_control_mapper_changes_with_context);
    RUN_TEST(test_progress_extrapolates_only_while_playing_and_clamps);
    RUN_TEST(test_backoff_is_bounded_and_resettable);
    RUN_TEST(test_pin_validation_detects_conflicts_and_unsafe_defaults);
    return UNITY_END();
}
