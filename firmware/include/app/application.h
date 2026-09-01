#pragma once

#include <Arduino.h>

#include <cstdint>

#include "app/messages.h"
#include "controls/input_manager.h"
#include "deskwave/core/input_logic.h"
#include "network/artwork_manager.h"
#include "network/network_manager.h"
#include "storage/settings_store.h"
#include "ui/ui_controller.h"

namespace deskwave::app {

class Application {
   public:
    Application(storage::SettingsStore& settingsStore, controls::InputManager& inputManager,
                network::NetworkManager& networkManager, network::ArtworkManager& artworkManager,
                ui::UiController& ui, QueueHandle_t inputQueue, QueueHandle_t playbackQueue,
                QueueHandle_t clockQueue, QueueHandle_t noticeQueue, QueueHandle_t commandQueue,
                QueueHandle_t feedbackQueue, QueueHandle_t artworkResultQueue,
                QueueHandle_t playerQueue);
    [[nodiscard]] bool begin();
    void loop();

   private:
    void consumeQueues(std::uint32_t nowMs);
    void consumeInput(std::uint32_t nowMs);
    void handleInput(const controls::InputEvent& event, std::uint32_t nowMs);
    void handleCommand(core::ControlCommand command, std::uint32_t nowMs);
    void adjustSetting(std::int8_t direction, std::uint32_t nowMs);
    void activateSetting(std::uint32_t nowMs);
    void updateSettingsView();
    void requestPlayers(std::uint32_t nowMs);
    void selectRelativePlayer(std::int8_t direction);
    void selectPlayer(std::uint32_t nowMs);
    [[nodiscard]] bool sendRequest(const ControlRequest& request, const char* failureMessage,
                                   std::uint32_t nowMs);
    [[nodiscard]] bool mediaControlAvailable(std::uint32_t nowMs);
    [[nodiscard]] bool handleFactoryResetChord(std::uint32_t nowMs);
    void requestFactoryResetConfirmation(std::uint32_t nowMs);
    void performFactoryReset();
    void persistSettingsIfDue(std::uint32_t nowMs);
    void updateHealth(std::uint32_t nowMs);
    void updateStatusLed(std::uint32_t nowMs);
    void syncArtworkProtection();
    void acknowledgeArtworkResult(const ArtworkResult& result);
    [[nodiscard]] static bool hostState(core::SystemState state) noexcept;

    storage::SettingsStore& settingsStore_;
    controls::InputManager& inputManager_;
    network::NetworkManager& networkManager_;
    network::ArtworkManager& artworkManager_;
    ui::UiController& ui_;
    QueueHandle_t inputQueue_;
    QueueHandle_t playbackQueue_;
    QueueHandle_t clockQueue_;
    QueueHandle_t noticeQueue_;
    QueueHandle_t commandQueue_;
    QueueHandle_t feedbackQueue_;
    QueueHandle_t artworkResultQueue_;
    QueueHandle_t playerQueue_;
    core::ControlMapper controlMapper_{};
    PlaybackSnapshot playback_{};
    PlayerListSnapshot players_{};
    ui::DeviceStatus deviceStatus_{};
    ui::SettingsView settings_{};
    std::uint32_t lastActivityAtMs_{0};
    std::uint32_t settingsChangedAtMs_{0};
    std::uint32_t lastHealthUpdateMs_{0};
    std::uint32_t lastHealthLogMs_{0};
    std::uint32_t lastPlayerRequestMs_{0};
    std::uint32_t lastStatusLedUpdateMs_{0};
    std::uint32_t factoryResetChordStartedAtMs_{0};
    std::int16_t optimisticVolumePercent_{-1};
    std::uint8_t selectedPlayer_{0};
    char reportedActiveArtworkId_[65]{};
    char reportedStagedArtworkId_[65]{};
    bool hasPlayback_{false};
    bool settingsDirty_{false};
    bool factoryResetChordTiming_{false};
    bool dimmed_{false};
    bool begun_{false};
};

}  // namespace deskwave::app
