#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>

#include "app/messages.h"
#include "deskwave/core/application_state.h"
#include "deskwave/core/backoff.h"
#include "network/provisioning_portal.h"
#include "storage/settings_store.h"

namespace deskwave::network {

class NetworkManager {
  public:
    NetworkManager(storage::SettingsStore& settingsStore, QueueHandle_t playbackQueue,
                   QueueHandle_t noticeQueue, QueueHandle_t commandQueue,
                   QueueHandle_t feedbackQueue, QueueHandle_t artworkQueue,
                   QueueHandle_t playerQueue);
    [[nodiscard]] bool begin();

  private:
    static void taskEntry(void* context);
    void run();
    [[nodiscard]] bool provision(storage::DeviceSettings& settings);
    [[nodiscard]] bool connectWifi(const storage::DeviceSettings& settings);
    [[nodiscard]] bool discoverHost(const storage::DeviceSettings& settings);
    [[nodiscard]] bool pairDevice(storage::DeviceSettings& settings);
    [[nodiscard]] bool runWebSocket(storage::DeviceSettings& settings);
    [[nodiscard]] bool postJson(const String& path, const String& requestBody,
                                int& responseCode, String& responseBody);
    [[nodiscard]] bool hostHealthy();
    void configureWebSocket(const String& token);
    void handleWebSocketEvent(WStype_t type, std::uint8_t* payload, std::size_t length);
    void handleProtocolMessage(const std::uint8_t* payload, std::size_t length);
    void handlePlaybackState(JsonObjectConst payload);
    void handleCommandResult(JsonObjectConst payload);
    void handlePlayers(JsonObjectConst payload);
    void processCommands();
    void sendCommand(const app::ControlRequest& request);
    void rejectQueuedCommands(const char* reason);
    void publishNotice(app::SystemNoticeType type, const char* primary,
                       const char* secondary = "", std::int32_t value = 0);
    void transition(core::StateEvent event, const char* primary, const char* secondary = "");
    [[nodiscard]] String deviceSuffix() const;
    [[nodiscard]] static const char* commandName(app::HostCommand command);

    storage::SettingsStore& settingsStore_;
    QueueHandle_t playbackQueue_;
    QueueHandle_t noticeQueue_;
    QueueHandle_t commandQueue_;
    QueueHandle_t feedbackQueue_;
    QueueHandle_t artworkQueue_;
    QueueHandle_t playerQueue_;
    ProvisioningPortal provisioningPortal_;
    WebSocketsClient webSocket_;
    core::StateMachine stateMachine_;
    core::ReconnectBackoff wifiBackoff_{1'000, 30'000};
    core::ReconnectBackoff hostBackoff_{500, 30'000};
    TaskHandle_t task_{nullptr};
    String deviceId_;
    String host_;
    std::uint16_t hostPort_{0};
    String activeToken_;
    std::uint32_t outgoingSequence_{0};
    std::uint32_t lastConnectedAtMs_{0};
    std::uint32_t disconnectedAtMs_{0};
    bool webSocketConnected_{false};
    bool mdnsStarted_{false};
};

}  // namespace deskwave::network
