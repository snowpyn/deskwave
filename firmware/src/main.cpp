#include <Arduino.h>

#include "app/application.h"
#include "app/messages.h"
#include "config/hardware_config.h"
#include "controls/input_manager.h"
#include "deskwave_version.h"
#include "display/display_driver.h"
#include "network/artwork_manager.h"
#include "network/network_manager.h"
#include "storage/settings_store.h"
#include "system/logging.h"
#include "ui/ui_controller.h"

namespace {

deskwave::app::Application* application = nullptr;
bool fatalStartupError = false;

bool queuesReady(const QueueHandle_t input, const QueueHandle_t playback,
                 const QueueHandle_t notices, const QueueHandle_t commands,
                 const QueueHandle_t feedback, const QueueHandle_t artworkRequests,
                 const QueueHandle_t artworkResults, const QueueHandle_t players) {
    return input != nullptr && playback != nullptr && notices != nullptr && commands != nullptr &&
           feedback != nullptr && artworkRequests != nullptr && artworkResults != nullptr &&
           players != nullptr;
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.printf("DeskWave %s\n", deskwave::kVersion);

    const auto inputQueue = xQueueCreate(32, sizeof(deskwave::controls::InputEvent));
    const auto playbackQueue = xQueueCreate(1, sizeof(deskwave::app::PlaybackSnapshot));
    const auto noticeQueue = xQueueCreate(8, sizeof(deskwave::app::SystemNotice));
    const auto commandQueue = xQueueCreate(16, sizeof(deskwave::app::ControlRequest));
    const auto feedbackQueue = xQueueCreate(8, sizeof(deskwave::app::CommandFeedback));
    const auto artworkRequestQueue = xQueueCreate(1, sizeof(deskwave::app::ArtworkRequest));
    const auto artworkResultQueue = xQueueCreate(1, sizeof(deskwave::app::ArtworkResult));
    const auto playerQueue = xQueueCreate(1, sizeof(deskwave::app::PlayerListSnapshot));
    if (!queuesReady(inputQueue, playbackQueue, noticeQueue, commandQueue, feedbackQueue,
                     artworkRequestQueue, artworkResultQueue, playerQueue)) {
        DW_LOG_ERROR("system", "Required application queues could not be allocated");
        fatalStartupError = true;
#if defined(DESKWAVE_ESP32_D0WD_V3)
        pinMode(deskwave::hardware::kStatusLedRed, OUTPUT);
        pinMode(deskwave::hardware::kStatusLedGreen, OUTPUT);
        pinMode(deskwave::hardware::kStatusLedBlue, OUTPUT);
#else
        pinMode(deskwave::hardware::kStatusLed, OUTPUT);
#endif
        return;
    }

    static deskwave::storage::SettingsStore settingsStore;
    static deskwave::controls::InputManager inputManager(inputQueue);
    static deskwave::display::DisplayDriver display;
    static deskwave::ui::UiController ui(display);
    static deskwave::network::ArtworkManager artworkManager(artworkRequestQueue,
                                                            artworkResultQueue);
    static deskwave::network::NetworkManager networkManager(
        settingsStore, playbackQueue, noticeQueue, commandQueue, feedbackQueue, artworkRequestQueue,
        playerQueue);
    static deskwave::app::Application controller(
        settingsStore, inputManager, networkManager, artworkManager, ui, inputQueue, playbackQueue,
        noticeQueue, commandQueue, feedbackQueue, artworkResultQueue, playerQueue);
    application = &controller;
    if (!application->begin()) {
        fatalStartupError = true;
        DW_LOG_ERROR("system", "DeskWave startup did not complete");
    }
}

void loop() {
    if (!fatalStartupError && application != nullptr) {
        application->loop();
        return;
    }
#if defined(DESKWAVE_ESP32_D0WD_V3)
    const auto enabled = (millis() / 150U) % 2U == 0;
    analogWrite(deskwave::hardware::kStatusLedRed, enabled ? 75 : 255);
    analogWrite(deskwave::hardware::kStatusLedGreen, 255);
    analogWrite(deskwave::hardware::kStatusLedBlue, 255);
#else
    digitalWrite(deskwave::hardware::kStatusLed, (millis() / 150U) % 2U == 0 ? HIGH : LOW);
#endif
    vTaskDelay(pdMS_TO_TICKS(20));
}
