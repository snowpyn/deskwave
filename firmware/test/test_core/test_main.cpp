#include <unity.h>

#include <array>

#include "app/messages.h"
#include "deskwave/core/application_state.h"
#include "deskwave/core/backoff.h"
#include "deskwave/core/clock.h"
#include "deskwave/core/input_logic.h"
#include "deskwave/core/pin_validation.h"
#include "deskwave/core/progress.h"
#include "deskwave/core/theme.h"

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
        ControlCommand::OpenActions,
        mapper.map(ControlContext::Playback, PhysicalControl::MoreButton, Gesture::ShortPress));
    TEST_ASSERT_EQUAL(
        ControlCommand::OpenActions,
        mapper.map(ControlContext::Actions, PhysicalControl::MoreButton, Gesture::ShortPress));
    TEST_ASSERT_EQUAL(
        ControlCommand::None,
        mapper.map(ControlContext::Playback, PhysicalControl::NoControl, Gesture::ShortPress));
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

    // The jitter calculation must saturate before converting back to uint32_t;
    // otherwise a large but valid maximum can wrap to a short delay.
    ReconnectBackoff largeBackoff(UINT32_MAX, UINT32_MAX);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, largeBackoff.next(UINT32_MAX));
}

void test_local_clock_formats_timezone_date_and_ampm() {
    ClockText text;
    TEST_ASSERT_TRUE(formatLocalClock(1'788'226'860'000ULL, -4 * 60 * 60, text));
    TEST_ASSERT_EQUAL_STRING("9:41", text.time);
    TEST_ASSERT_EQUAL_STRING("PM", text.period);
    TEST_ASSERT_EQUAL_STRING("Aug/31/2026", text.date);

    TEST_ASSERT_TRUE(formatLocalClock(0, 0, text));
    TEST_ASSERT_EQUAL_STRING("12:00", text.time);
    TEST_ASSERT_EQUAL_STRING("AM", text.period);
    TEST_ASSERT_EQUAL_STRING("Jan/01/1970", text.date);

    TEST_ASSERT_TRUE(formatLocalClock(1'709'208'300'000ULL, 0, text));
    TEST_ASSERT_EQUAL_STRING("12:05", text.time);
    TEST_ASSERT_EQUAL_STRING("PM", text.period);
    TEST_ASSERT_EQUAL_STRING("Feb/29/2024", text.date);

    TEST_ASSERT_FALSE(formatLocalClock(1'788'226'860'000ULL, 25 * 60 * 60, text));
    TEST_ASSERT_EQUAL_STRING("--:--", text.time);
    TEST_ASSERT_EQUAL_STRING("--", text.period);
    TEST_ASSERT_EQUAL_STRING("---/--/----", text.date);
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

void test_theme_rgb_packing_and_interpolation() {
    const Rgb888 color = unpackRgb(0x12ABEFU);
    TEST_ASSERT_EQUAL_UINT8(0x12, color.red);
    TEST_ASSERT_EQUAL_UINT8(0xAB, color.green);
    TEST_ASSERT_EQUAL_UINT8(0xEF, color.blue);
    TEST_ASSERT_EQUAL_UINT32(0x12ABEFU, packRgb(color));

    const Rgb888 black{};
    const Rgb888 white{255, 255, 255};
    const auto midpoint = interpolateColor(black, white, 128);
    TEST_ASSERT_EQUAL_UINT8(128, midpoint.red);
    TEST_ASSERT_EQUAL_UINT8(128, midpoint.green);
    TEST_ASSERT_EQUAL_UINT8(128, midpoint.blue);
    TEST_ASSERT_TRUE(colorsEqual(black, interpolateColor(black, white, 0)));
    TEST_ASSERT_TRUE(colorsEqual(white, interpolateColor(black, white, 255)));

    const Rgb888 accent{200, 100, 50};
    const auto halfBrightness = scaleColor(accent, 128);
    TEST_ASSERT_EQUAL_UINT8(100, halfBrightness.red);
    TEST_ASSERT_EQUAL_UINT8(50, halfBrightness.green);
    TEST_ASSERT_EQUAL_UINT8(25, halfBrightness.blue);
    TEST_ASSERT_TRUE(colorsEqual(Rgb888{}, scaleColor(accent, 0)));
    TEST_ASSERT_TRUE(colorsEqual(accent, scaleColor(accent, 255)));

    TEST_ASSERT_TRUE(colorsEqual(Rgb888{}, normalizeColor(Rgb888{})));
    TEST_ASSERT_TRUE(colorsEqual(Rgb888{255, 128, 64}, normalizeColor(Rgb888{64, 32, 16})));
    TEST_ASSERT_TRUE(colorsEqual(Rgb888{220, 251, 255}, normalizeColor(Rgb888{57, 65, 66})));
    TEST_ASSERT_TRUE(
        colorsEqual(Rgb888{190, 170, 240},
                    rgbLedPwm(normalizeColor(Rgb888{57, 65, 66}), 255, Rgb888{255, 176, 240})));
    TEST_ASSERT_TRUE(colorsEqual(Rgb888{255, 22, 8}, rgbLedPwm(normalizeColor(Rgb888{90, 32, 16}),
                                                               255, Rgb888{255, 176, 240})));
    TEST_ASSERT_TRUE(colorsEqual(Rgb888{38, 34, 48}, rgbLedPwm(normalizeColor(Rgb888{57, 65, 66}),
                                                               51, Rgb888{255, 176, 240})));

    const auto ledMidpoint = rgbLedPwm(Rgb888{128, 128, 128}, 255, Rgb888{255, 255, 255});
    TEST_ASSERT_EQUAL_UINT8(64, ledMidpoint.red);
    TEST_ASSERT_EQUAL_UINT8(64, ledMidpoint.green);
    TEST_ASSERT_EQUAL_UINT8(64, ledMidpoint.blue);
    TEST_ASSERT_TRUE(colorsEqual(Rgb888{}, rgbLedPwm(accent, 0, Rgb888{255, 255, 255})));
    TEST_ASSERT_TRUE(
        colorsEqual(Rgb888{255, 176, 240}, rgbLedPwm(white, 255, Rgb888{255, 176, 240})));
}

void test_theme_interruption_is_continuous_and_eased() {
    const ThemePalette first{{10, 20, 30}, {40, 50, 60}, {1, 2, 3}, {240, 241, 242}};
    const ThemePalette second{{110, 120, 130}, {140, 150, 160}, {11, 12, 13}, {230, 231, 232}};
    const ThemePalette third{{210, 200, 190}, {180, 170, 160}, {21, 22, 23}, {220, 221, 222}};
    const auto interrupted = interpolateTheme(first, second, 96);
    TEST_ASSERT_TRUE(themesEqual(interrupted, interpolateTheme(interrupted, third, 0)));
    TEST_ASSERT_TRUE(themesEqual(third, interpolateTheme(interrupted, third, 255)));

    TEST_ASSERT_EQUAL_UINT8(0, easedProgress(0, 750));
    TEST_ASSERT_UINT8_WITHIN(4, 128, easedProgress(375, 750));
    TEST_ASSERT_EQUAL_UINT8(255, easedProgress(750, 750));
    const std::uint32_t started = UINT32_MAX - 15U;
    const std::uint32_t now = 16U;
    TEST_ASSERT_EQUAL_UINT8(255, easedProgress(static_cast<std::uint32_t>(now - started), 32));
}

void test_theme_softening_desaturates_and_dims() {
    const Rgb888 red{255, 0, 0};
    const Rgb888 black{};
    const auto neutral = softenColor(red, black, 255, 0);
    TEST_ASSERT_EQUAL_UINT8(neutral.red, neutral.green);
    TEST_ASSERT_EQUAL_UINT8(neutral.green, neutral.blue);
    TEST_ASSERT_TRUE(neutral.red > 0);
    TEST_ASSERT_TRUE(colorsEqual(black, softenColor(red, black, 0, 255)));
    TEST_ASSERT_TRUE(colorsEqual(red, softenColor(red, black, 0, 0)));
}

void test_theme_and_generation_survive_artwork_messages() {
    constexpr char artworkA[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    constexpr char artworkB[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    deskwave::app::PlaybackSnapshot snapshot;
    snapshot.artworkGeneration = 41;
    snapshot.hasTheme = true;
    snapshot.theme = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {240, 241, 242}};

    deskwave::app::ArtworkRequest request;
    request.artworkGeneration = snapshot.artworkGeneration;
    request.hasTheme = snapshot.hasTheme;
    request.theme = snapshot.theme;
    deskwave::app::ArtworkResult result;
    result.artworkGeneration = request.artworkGeneration;
    result.hasTheme = request.hasTheme;
    result.theme = request.theme;

    TEST_ASSERT_EQUAL_UINT32(41, result.artworkGeneration);
    TEST_ASSERT_TRUE(result.hasTheme);
    TEST_ASSERT_TRUE(themesEqual(snapshot.theme, result.theme));
    TEST_ASSERT_EQUAL_UINT32(0, deskwave::app::PlaybackSnapshot{}.artworkGeneration);
    TEST_ASSERT_TRUE(deskwave::app::artworkIdentityMatches(artworkA, 41, artworkA, 41));
    TEST_ASSERT_FALSE(deskwave::app::artworkIdentityMatches(artworkA, 41, artworkA, 42));
    TEST_ASSERT_FALSE(deskwave::app::artworkIdentityMatches(artworkA, 41, artworkB, 41));
    TEST_ASSERT_FALSE(deskwave::app::artworkIdentityMatches("", 41, artworkA, 41));
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
    RUN_TEST(test_local_clock_formats_timezone_date_and_ampm);
    RUN_TEST(test_pin_validation_detects_conflicts_and_unsafe_defaults);
    RUN_TEST(test_theme_rgb_packing_and_interpolation);
    RUN_TEST(test_theme_interruption_is_continuous_and_eased);
    RUN_TEST(test_theme_softening_desaturates_and_dims);
    RUN_TEST(test_theme_and_generation_survive_artwork_messages);
    return UNITY_END();
}
