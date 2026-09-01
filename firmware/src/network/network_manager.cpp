#include "network/network_manager.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>
#include <cmath>

#include "config/build_config.h"
#include "system/logging.h"

namespace deskwave::network {
namespace {

constexpr std::size_t kMaximumHttpBody = 2'048;
constexpr std::uint32_t kWebSocketDiscoveryTimeoutMs = 30'000;
constexpr std::uint32_t kMaximumProtocolSequence = 2'147'483'647;

bool isHexIdentifier(const char* value) {
    if (value == nullptr || std::strlen(value) != 64) {
        return false;
    }
    for (std::size_t index = 0; index < 64; ++index) {
        const char character = value[index];
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

std::uint64_t boundedMilliseconds(const JsonVariantConst value, const std::uint64_t maximum) {
    if (!value.is<std::uint64_t>()) {
        return 0;
    }
    return std::min(value.as<std::uint64_t>(), maximum);
}

bool parsePackedColor(const JsonVariantConst value, core::Rgb888& destination) {
    if (!value.is<std::uint32_t>()) {
        return false;
    }
    const auto packed = value.as<std::uint32_t>();
    if (packed > 0xFFFFFFU) {
        return false;
    }
    destination = core::unpackRgb(packed);
    return true;
}

bool parseTheme(const JsonVariantConst value, core::ThemePalette& destination) {
    if (!value.is<JsonObjectConst>()) {
        return false;
    }
    const auto theme = value.as<JsonObjectConst>();
    core::ThemePalette parsed;
    if (!parsePackedColor(theme["primary"], parsed.primary) ||
        !parsePackedColor(theme["secondary"], parsed.secondary) ||
        !parsePackedColor(theme["background"], parsed.background) ||
        !parsePackedColor(theme["foreground"], parsed.foreground)) {
        return false;
    }
    destination = parsed;
    return true;
}

bool validArtworkPath(const char* path, const char* artworkId) {
    if (path == nullptr || !isHexIdentifier(artworkId)) {
        return false;
    }
    char expectedPath[96];
    std::snprintf(expectedPath, sizeof(expectedPath), "/v1/artwork/%s.jpg", artworkId);
    return std::strcmp(path, expectedPath) == 0;
}

}  // namespace

NetworkManager::NetworkManager(storage::SettingsStore& settingsStore,
                               const QueueHandle_t playbackQueue, const QueueHandle_t clockQueue,
                               const QueueHandle_t noticeQueue, const QueueHandle_t commandQueue,
                               const QueueHandle_t feedbackQueue, const QueueHandle_t artworkQueue,
                               const QueueHandle_t playerQueue)
    : settingsStore_(settingsStore),
      playbackQueue_(playbackQueue),
      clockQueue_(clockQueue),
      noticeQueue_(noticeQueue),
      commandQueue_(commandQueue),
      feedbackQueue_(feedbackQueue),
      artworkQueue_(artworkQueue),
      playerQueue_(playerQueue),
      provisioningPortal_(settingsStore) {
    const auto chipId = static_cast<std::uint32_t>(ESP.getEfuseMac());
    char identifier[20];
    std::snprintf(identifier, sizeof(identifier), "dw-%08lx", static_cast<unsigned long>(chipId));
    deviceId_ = identifier;
}

bool NetworkManager::begin() {
    if (task_ != nullptr) {
        return true;
    }
    return xTaskCreatePinnedToCore(taskEntry, "deskwave-network", 10'240, this, 2, &task_, 0) ==
           pdPASS;
}

void NetworkManager::taskEntry(void* context) {
    static_cast<NetworkManager*>(context)->run();
    vTaskDelete(nullptr);
}

String NetworkManager::deviceSuffix() const { return deviceId_.substring(deviceId_.length() - 4); }

void NetworkManager::publishNotice(const app::SystemNoticeType type, const char* primary,
                                   const char* secondary, const std::int32_t value) {
    app::SystemNotice notice;
    notice.type = type;
    notice.state = stateMachine_.state();
    app::copyText(notice.primary, primary);
    app::copyText(notice.secondary, secondary);
    notice.value = value;
    if (xQueueSend(noticeQueue_, &notice, 0) != pdTRUE) {
        app::SystemNotice discarded;
        xQueueReceive(noticeQueue_, &discarded, 0);
        xQueueSend(noticeQueue_, &notice, 0);
    }
}

void NetworkManager::transition(const core::StateEvent event, const char* primary,
                                const char* secondary) {
    if (stateMachine_.transition(event)) {
        publishNotice(app::SystemNoticeType::StateChanged, primary, secondary);
    }
}

void NetworkManager::run() {
    storage::DeviceSettings settings;
    auto loadStatus = settingsStore_.load(settings);
    if (loadStatus == storage::SettingsLoadStatus::Unsupported ||
        loadStatus == storage::SettingsLoadStatus::Corrupt) {
        transition(core::StateEvent::FatalError, "Settings unavailable",
                   "Hold Left + Right + Menu for 5 seconds to factory reset");
        DW_LOG_ERROR("storage", "Settings cannot be used safely (status %u)",
                     static_cast<unsigned>(loadStatus));
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1'000));
        }
    }
    const bool bootstrapRequested =
        config::kBootstrapWifiEnabled &&
        (loadStatus == storage::SettingsLoadStatus::Empty ||
         (config::kForceBootstrapWifi && settings.wifiSsid != config::kBootstrapWifiSsid));
    if (bootstrapRequested) {
        if (!settingsStore_.saveWifi(config::kBootstrapWifiSsid, config::kBootstrapWifiPassword)) {
            transition(core::StateEvent::FatalError, "Wi-Fi profile unavailable",
                       "Private bootstrap profile could not be saved");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1'000));
            }
        }
        loadStatus = settingsStore_.load(settings);
        if (loadStatus != storage::SettingsLoadStatus::Ok &&
            loadStatus != storage::SettingsLoadStatus::Migrated) {
            transition(core::StateEvent::FatalError, "Wi-Fi profile unavailable",
                       "Saved profile could not be loaded");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1'000));
            }
        }
        DW_LOG_INFO("network", "Private first-boot Wi-Fi profile installed");
    }
    if (!settings.wifiConfigured) {
        transition(core::StateEvent::BootWithoutCredentials, "Wi-Fi setup required");
        if (!provision(settings)) {
            transition(core::StateEvent::FatalError, "Provisioning failed");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1'000));
            }
        }
    } else {
        transition(core::StateEvent::BootWithCredentials, "Connecting to Wi-Fi");
    }

    while (true) {
        if (!connectWifi(settings)) {
            continue;
        }
        while (WiFi.status() == WL_CONNECTED) {
            if (!discoverHost(settings)) {
                continue;
            }
            if (!settings.paired) {
                transition(core::StateEvent::PairingNeeded, "Pair with DeskWave Host");
                if (!pairDevice(settings)) {
                    transition(core::StateEvent::HostDisconnected, "DeskWave Host offline");
                    continue;
                }
                transition(core::StateEvent::PairingComplete, "Pairing approved");
            } else {
                transition(core::StateEvent::HostDiscovered, "DeskWave Host found");
            }
            if (!runWebSocket(settings)) {
                rejectQueuedCommands("DeskWave Host is offline");
            }
        }
        webSocket_.disconnect();
        webSocketConnected_ = false;
        transition(core::StateEvent::WifiLost, "Wi-Fi unavailable", "Reconnecting");
    }
}

bool NetworkManager::provision(storage::DeviceSettings& settings) {
    if (!provisioningPortal_.begin(deviceSuffix())) {
        return false;
    }
    publishNotice(app::SystemNoticeType::ProvisioningStarted,
                  provisioningPortal_.accessPointName().c_str(),
                  provisioningPortal_.accessPointPassword().c_str());
    while (!provisioningPortal_.credentialsSaved()) {
        provisioningPortal_.loop();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    while (provisioningPortal_.credentialsSaved() && WiFi.getMode() == WIFI_AP_STA) {
        provisioningPortal_.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
        if (WiFi.softAPIP() == IPAddress()) {
            break;
        }
    }
    const auto status = settingsStore_.load(settings);
    if (status != storage::SettingsLoadStatus::Ok &&
        status != storage::SettingsLoadStatus::Migrated) {
        return false;
    }
    transition(core::StateEvent::CredentialsSaved, "Connecting to Wi-Fi");
    return settings.wifiConfigured;
}

bool NetworkManager::connectWifi(const storage::DeviceSettings& settings) {
    if (stateMachine_.state() == core::SystemState::Offline) {
        const auto waitMs = wifiBackoff_.next(esp_random());
        vTaskDelay(pdMS_TO_TICKS(waitMs));
        transition(core::StateEvent::Retry, "Connecting to Wi-Fi");
    }
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.persistent(false);
    WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPassword.c_str());
    const auto startedAt = millis();
    while (WiFi.status() != WL_CONNECTED &&
           static_cast<std::uint32_t>(millis() - startedAt) < config::kWifiAttemptTimeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(false, false);
        transition(core::StateEvent::WifiUnavailable, "Wi-Fi unavailable", "Reconnecting");
        return false;
    }
    wifiBackoff_.reset();
    transition(core::StateEvent::WifiConnected, "Finding DeskWave Host");
    if (!mdnsStarted_) {
        const String hostname = "deskwave-" + deviceSuffix();
        mdnsStarted_ = MDNS.begin(hostname.c_str());
        if (!mdnsStarted_) {
            DW_LOG_WARN("network", "mDNS client initialization failed");
        }
    }
    publishNotice(app::SystemNoticeType::NetworkDetails, WiFi.localIP().toString().c_str(),
                  settings.wifiSsid.c_str(), WiFi.RSSI());
    DW_LOG_INFO("network", "Connected to Wi-Fi with RSSI %d dBm", WiFi.RSSI());
    return true;
}

bool NetworkManager::discoverHost(const storage::DeviceSettings& settings) {
    if (!settings.hostOverride.isEmpty()) {
        host_ = settings.hostOverride;
        hostPort_ = settings.hostPort;
        hostBackoff_.reset();
        return true;
    }
    if (!mdnsStarted_) {
        vTaskDelay(pdMS_TO_TICKS(hostBackoff_.next(esp_random())));
        return false;
    }
    const int count = MDNS.queryService(config::kMdnsService, config::kMdnsProtocol);
    if (count <= 0) {
        // Keep discovery quiet while the backoff clock runs. The current
        // "Finding DeskWave Host" state is already visible and actionable;
        // repeating error toasts made a healthy retry loop look like a fault.
        vTaskDelay(pdMS_TO_TICKS(hostBackoff_.next(esp_random())));
        return false;
    }
    host_ = MDNS.IP(0).toString();
    hostPort_ = MDNS.port(0);
    if (host_.isEmpty() || hostPort_ == 0) {
        vTaskDelay(pdMS_TO_TICKS(hostBackoff_.next(esp_random())));
        return false;
    }
    hostBackoff_.reset();
    DW_LOG_INFO("host", "Discovered DeskWave Host at %s:%u", host_.c_str(), hostPort_);
    return true;
}

bool NetworkManager::postJson(const String& path, const String& requestBody, int& responseCode,
                              String& responseBody) {
    WiFiClient client;
    client.setTimeout(config::kHttpTimeoutMs / 1'000);
    HTTPClient http;
    http.setConnectTimeout(config::kHttpTimeoutMs);
    http.setTimeout(config::kHttpTimeoutMs);
    const String url = "http://" + host_ + ":" + String(hostPort_) + path;
    if (!http.begin(client, url)) {
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    responseCode = http.POST(requestBody);
    if (responseCode <= 0) {
        http.end();
        return false;
    }
    const int contentLength = http.getSize();
    if (contentLength > static_cast<int>(kMaximumHttpBody)) {
        http.end();
        return false;
    }
    responseBody = http.getString();
    http.end();
    return responseBody.length() <= kMaximumHttpBody;
}

bool NetworkManager::pairDevice(storage::DeviceSettings& settings) {
    char pairingCode[7];
    std::snprintf(pairingCode, sizeof(pairingCode), "%06lu",
                  static_cast<unsigned long>(esp_random() % 1'000'000));
    publishNotice(app::SystemNoticeType::PairingCode, pairingCode,
                  "Run: deskwave-host pair <code>");

    JsonDocument requestDocument;
    requestDocument["device_id"] = deviceId_;
    requestDocument["name"] = "DeskWave " + deviceSuffix();
    requestDocument["code"] = pairingCode;
    String body;
    serializeJson(requestDocument, body);
    int responseCode = 0;
    String responseBody;
    if (!postJson("/v1/pairing/request", body, responseCode, responseBody) ||
        responseCode != HTTP_CODE_ACCEPTED) {
        DW_LOG_WARN("pairing", "Pairing request failed with HTTP %d", responseCode);
        return false;
    }

    JsonDocument statusDocument;
    statusDocument["device_id"] = deviceId_;
    statusDocument["code"] = pairingCode;
    body.clear();
    serializeJson(statusDocument, body);
    const auto expiresAt = millis() + 300'000;
    while (WiFi.status() == WL_CONNECTED && static_cast<std::int32_t>(millis() - expiresAt) < 0) {
        vTaskDelay(pdMS_TO_TICKS(config::kPairingPollMs));
        responseBody.clear();
        if (!postJson("/v1/pairing/status", body, responseCode, responseBody)) {
            return false;
        }
        if (responseCode == HTTP_CODE_ACCEPTED) {
            continue;
        }
        if (responseCode != HTTP_CODE_OK) {
            return false;
        }
        JsonDocument response;
        if (deserializeJson(response, responseBody) != DeserializationError::Ok ||
            response["status"] != "paired" || !response["token"].is<const char*>()) {
            return false;
        }
        const String token = response["token"].as<String>();
        if (token.length() < 32 || token.length() > 128 || !settingsStore_.saveToken(token)) {
            return false;
        }
        settings.hostToken = token;
        settings.paired = true;
        DW_LOG_INFO("pairing", "Device pairing completed");
        return true;
    }
    return false;
}

void NetworkManager::configureWebSocket(const String& token) {
    activeToken_ = token;
    const String authorization = "Bearer " + token;
    const auto reconnectIntervalMs = webSocketBackoff_.next(esp_random());
    // WebSocketsClient::begin() clears its authorization fields, so configure
    // the endpoint before installing the bearer header.
    webSocket_.begin(host_.c_str(), hostPort_, config::kWebSocketPath, "");
    webSocket_.setAuthorization(authorization.c_str());
    webSocket_.setReconnectInterval(reconnectIntervalMs);
    webSocket_.enableHeartbeat(15'000, 3'000, 2);
    webSocket_.onEvent(
        [this](const WStype_t type, std::uint8_t* payload, const std::size_t length) {
            handleWebSocketEvent(type, payload, length);
        });
    disconnectedAtMs_ = millis();
}

bool NetworkManager::runWebSocket(storage::DeviceSettings& settings) {
    configureWebSocket(settings.hostToken);
    while (WiFi.status() == WL_CONNECTED) {
        webSocket_.loop();
        processCommands();
        if (webSocketConnected_) {
            disconnectedAtMs_ = 0;
        } else if (disconnectedAtMs_ == 0) {
            disconnectedAtMs_ = millis();
        } else if (static_cast<std::uint32_t>(millis() - disconnectedAtMs_) >=
                   kWebSocketDiscoveryTimeoutMs) {
            webSocket_.disconnect();
            if (hostHealthy()) {
                DW_LOG_WARN("host", "Host is healthy but rejected the stored device session");
                if (settingsStore_.clearToken()) {
                    settings.paired = false;
                    settings.hostToken.clear();
                }
            }
            transition(core::StateEvent::HostDisconnected, "DeskWave Host offline", "Reconnecting");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

bool NetworkManager::hostHealthy() {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(2'000);
    http.setTimeout(2'000);
    const String url = "http://" + host_ + ":" + String(hostPort_) + "/healthz";
    if (!http.begin(client, url)) {
        return false;
    }
    const int code = http.GET();
    http.end();
    return code == HTTP_CODE_OK;
}

void NetworkManager::handleWebSocketEvent(const WStype_t type, std::uint8_t* payload,
                                          const std::size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            webSocketConnected_ = true;
            webSocketBackoff_.reset();
            lastConnectedAtMs_ = millis();
            transition(core::StateEvent::HostConnected, "Connected");
            DW_LOG_INFO("host", "Authenticated WebSocket connected");
            break;
        case WStype_DISCONNECTED:
            if (webSocketConnected_) {
                webSocketConnected_ = false;
                disconnectedAtMs_ = millis();
                transition(core::StateEvent::HostDisconnected, "DeskWave Host offline",
                           "Reconnecting");
            }
            break;
        case WStype_TEXT:
            handleProtocolMessage(payload, length);
            break;
        case WStype_ERROR:
            DW_LOG_WARN("host", "WebSocket transport error");
            break;
        default:
            break;
    }
}

void NetworkManager::handleProtocolMessage(const std::uint8_t* payload, const std::size_t length) {
    if (length == 0 || length > config::kMaximumProtocolMessageBytes) {
        DW_LOG_WARN("protocol", "Rejected message with invalid size %u",
                    static_cast<unsigned>(length));
        return;
    }
    JsonDocument document;
    const auto error = deserializeJson(document, payload, length);
    if (error != DeserializationError::Ok || document["protocol"] != 1 ||
        !document["type"].is<const char*>() || !document["sequence"].is<std::uint32_t>() ||
        !document["payload"].is<JsonObjectConst>()) {
        DW_LOG_WARN("protocol", "Rejected malformed protocol message");
        return;
    }
    if (document["timestamp_ms"].is<std::uint64_t>() &&
        document["utc_offset_seconds"].is<std::int32_t>()) {
        const auto offset = document["utc_offset_seconds"].as<std::int32_t>();
        if (offset >= -24 * 60 * 60 && offset <= 24 * 60 * 60) {
            app::ClockSync clock;
            clock.unixMs = document["timestamp_ms"].as<std::uint64_t>();
            clock.utcOffsetSeconds = offset;
            clock.receivedAtMs = millis();
            clock.valid = true;
            xQueueOverwrite(clockQueue_, &clock);
        }
    }
    const String type = document["type"].as<String>();
    const JsonObjectConst body = document["payload"].as<JsonObjectConst>();
    if (type == "playback_state") {
        handlePlaybackState(body);
    } else if (type == "command_result") {
        handleCommandResult(body);
    } else if (type == "players") {
        handlePlayers(body);
    } else if (type == "clock_sync") {
        // The envelope already refreshed the wall clock. No payload is required.
    } else if (type == "hello") {
        if (body["protocol"] != 1) {
            publishNotice(app::SystemNoticeType::RecoverableError, "Protocol mismatch");
        }
    } else if (type == "error") {
        publishNotice(app::SystemNoticeType::RecoverableError, "Host rejected a message",
                      body["message"] | "Unknown protocol error");
    }
}

void NetworkManager::handlePlaybackState(const JsonObjectConst payload) {
    if (!payload["title"].is<const char*>() || !payload["artists"].is<JsonArrayConst>() ||
        !payload["album"].is<const char*>() || !payload["status"].is<const char*>() ||
        !payload["capabilities"].is<JsonObjectConst>()) {
        DW_LOG_WARN("protocol", "Playback state omitted required fields");
        return;
    }
    app::PlaybackSnapshot snapshot;
    app::copyText(snapshot.title, payload["title"].as<const char*>());
    const JsonArrayConst artists = payload["artists"].as<JsonArrayConst>();
    String artistText;
    for (std::size_t index = 0; index < artists.size() && index < 3; ++index) {
        if (!artists[index].is<const char*>()) {
            continue;
        }
        if (!artistText.isEmpty()) {
            artistText += ", ";
        }
        artistText += artists[index].as<const char*>();
        if (artistText.length() >= sizeof(snapshot.artist) - 1) {
            break;
        }
    }
    app::copyText(snapshot.artist, artistText.c_str());
    app::copyText(snapshot.album, payload["album"].as<const char*>());
    app::copyText(snapshot.playerName, payload["player_name"] | "");
    app::copyText(snapshot.playerId, payload["player_id"] | "");
    app::copyText(snapshot.trackId, payload["track_id"] | "");
    snapshot.hasTheme = parseTheme(payload["theme"], snapshot.theme);
    if (payload["artwork_generation"].is<std::uint32_t>()) {
        snapshot.artworkGeneration = payload["artwork_generation"].as<std::uint32_t>();
    }
    snapshot.hasDuration = payload["duration_ms"].is<std::uint64_t>();
    snapshot.durationMs = boundedMilliseconds(payload["duration_ms"], 7ULL * 24 * 60 * 60 * 1000);
    snapshot.positionMs = boundedMilliseconds(
        payload["position_ms"], snapshot.durationMs == 0 ? UINT64_MAX : snapshot.durationMs);
    snapshot.receivedAtMs = millis();
    const String status = payload["status"].as<String>();
    if (status == "playing") {
        snapshot.status = app::PlaybackStatus::Playing;
        transition(core::StateEvent::PlaybackStarted, "Playing");
    } else if (status == "paused") {
        snapshot.status = app::PlaybackStatus::Paused;
        transition(core::StateEvent::PlaybackPaused, "Paused");
    } else if (status == "stopped") {
        snapshot.status = app::PlaybackStatus::Stopped;
        transition(core::StateEvent::PlaybackStopped, "Ready");
    } else {
        DW_LOG_WARN("protocol", "Playback status is invalid");
        return;
    }
    if (payload["volume"].is<float>()) {
        const float volume = payload["volume"].as<float>();
        if (std::isfinite(volume) && volume >= 0.0F && volume <= 1.0F) {
            snapshot.volumePercent = static_cast<std::int16_t>(std::lround(volume * 100.0F));
        }
    }
    snapshot.mutedKnown = payload["muted"].is<bool>();
    snapshot.muted = payload["muted"] | false;
    snapshot.shuffleKnown = payload["shuffle"].is<bool>();
    snapshot.shuffle = payload["shuffle"] | false;
    if (payload["repeat"].is<const char*>()) {
        const String repeat = payload["repeat"].as<String>();
        if (repeat == "off") {
            snapshot.repeat = app::RepeatMode::Off;
        } else if (repeat == "track") {
            snapshot.repeat = app::RepeatMode::Track;
        } else if (repeat == "playlist") {
            snapshot.repeat = app::RepeatMode::Playlist;
        }
    }
    const JsonObjectConst capabilities = payload["capabilities"].as<JsonObjectConst>();
    snapshot.canSeek = capabilities["seek"] | false;
    snapshot.canNext = capabilities["next"] | false;
    snapshot.canPrevious = capabilities["previous"] | false;
    snapshot.canControl = capabilities["control"] | false;
    snapshot.queueAvailable = capabilities["queue"] | false;
    if (payload["queue"].is<JsonArrayConst>()) {
        const JsonArrayConst queue = payload["queue"].as<JsonArrayConst>();
        for (const JsonObjectConst entry : queue) {
            if (snapshot.queueCount >= app::kMaximumQueueItems ||
                !entry["title"].is<const char*>() || !entry["artist"].is<const char*>()) {
                continue;
            }
            auto& destination = snapshot.queue[snapshot.queueCount];
            app::copyText(destination.title, entry["title"].as<const char*>());
            app::copyText(destination.artist, entry["artist"].as<const char*>());
            app::copyText(destination.trackId, entry["track_id"] | "");
            ++snapshot.queueCount;
        }
    }

    app::ArtworkRequest artworkRequest;
    bool hasArtworkRequest = false;
    const JsonVariantConst artworkIdValue = payload["artwork_id"];
    const JsonVariantConst artworkPathValue = payload["artwork_path"];
    const bool hasArtworkIdField = !artworkIdValue.isUnbound();
    const bool hasArtworkPathField = !artworkPathValue.isUnbound();
    const bool artworkIdIsString = artworkIdValue.is<const char*>();
    const bool artworkPathIsString = artworkPathValue.is<const char*>();
    const char* artworkId = artworkIdIsString ? artworkIdValue.as<const char*>() : "";
    const char* artworkPath = artworkPathIsString ? artworkPathValue.as<const char*>() : "";
    const bool explicitNullArtwork = hasArtworkIdField && hasArtworkPathField &&
                                     artworkIdValue.isNull() && artworkPathValue.isNull();
    const bool validArtworkReference =
        artworkIdIsString && artworkPathIsString && validArtworkPath(artworkPath, artworkId);
    if (validArtworkReference && (snapshot.artworkGeneration == 0 || snapshot.hasTheme)) {
        app::copyText(snapshot.artworkId, artworkId);
        app::copyText(snapshot.artworkPath, artworkPath);
        app::copyText(artworkRequest.host, host_.c_str());
        artworkRequest.port = hostPort_;
        app::copyText(artworkRequest.path, artworkPath);
        app::copyText(artworkRequest.artworkId, artworkId);
        app::copyText(artworkRequest.token, activeToken_.c_str());
        artworkRequest.theme = snapshot.theme;
        artworkRequest.artworkGeneration = snapshot.artworkGeneration;
        artworkRequest.hasTheme = snapshot.hasTheme;
        hasArtworkRequest = true;
    } else if (explicitNullArtwork && snapshot.artworkGeneration != 0 && snapshot.hasTheme) {
        // A non-zero generation with a complete theme and no artwork is the
        // host's authoritative fallback bundle. Generation zero remains
        // backward-compatible and cannot evict a validated cover.
    } else if (!explicitNullArtwork || snapshot.artworkGeneration != 0) {
        // Missing, mixed, malformed, or incomplete visual fields are not an
        // authoritative fallback. Strip their visual tuple so the UI retains
        // its last acknowledged cover and palette.
        snapshot.artworkGeneration = 0;
        snapshot.hasTheme = false;
        DW_LOG_WARN("protocol", "Ignored malformed artwork/theme bundle");
    }
    // Publish playback intent before enabling even a warm-cache artwork result
    // so the main loop can stage or commit the exact matching bundle.
    xQueueOverwrite(playbackQueue_, &snapshot);
    if (hasArtworkRequest) {
        xQueueOverwrite(artworkQueue_, &artworkRequest);
    }
}

void NetworkManager::handleCommandResult(const JsonObjectConst payload) {
    if (!payload["request_sequence"].is<std::uint32_t>() || !payload["success"].is<bool>() ||
        !payload["command"].is<const char*>()) {
        return;
    }
    app::CommandFeedback feedback;
    feedback.requestSequence = payload["request_sequence"].as<std::uint32_t>();
    feedback.success = payload["success"].as<bool>();
    app::copyText(feedback.command, payload["command"].as<const char*>());
    app::copyText(feedback.error, payload["error"] | "");
    if (xQueueSend(feedbackQueue_, &feedback, 0) != pdTRUE) {
        app::CommandFeedback discarded;
        xQueueReceive(feedbackQueue_, &discarded, 0);
        xQueueSend(feedbackQueue_, &feedback, 0);
    }
}

void NetworkManager::handlePlayers(const JsonObjectConst payload) {
    if (!payload["players"].is<JsonArrayConst>()) {
        return;
    }
    app::PlayerListSnapshot snapshot;
    const JsonArrayConst players = payload["players"].as<JsonArrayConst>();
    for (const JsonObjectConst player : players) {
        if (snapshot.count >= app::kMaximumPlayers || !player["id"].is<const char*>() ||
            !player["name"].is<const char*>() || !player["status"].is<const char*>()) {
            continue;
        }
        auto& destination = snapshot.players[snapshot.count];
        const String id = player["id"].as<String>();
        const String name = player["name"].as<String>();
        const String status = player["status"].as<String>();
        if (id.isEmpty() || id.length() >= sizeof(destination.id) || name.isEmpty() ||
            name.length() >= sizeof(destination.name)) {
            continue;
        }
        app::copyText(destination.id, id.c_str());
        app::copyText(destination.name, name.c_str());
        if (status == "playing") {
            destination.status = app::PlaybackStatus::Playing;
        } else if (status == "paused") {
            destination.status = app::PlaybackStatus::Paused;
        } else if (status == "stopped") {
            destination.status = app::PlaybackStatus::Stopped;
        } else {
            continue;
        }
        ++snapshot.count;
    }
    snapshot.receivedAtMs = millis();
    xQueueOverwrite(playerQueue_, &snapshot);
}

const char* NetworkManager::commandName(const app::HostCommand command) {
    switch (command) {
        case app::HostCommand::Play:
            return "play";
        case app::HostCommand::Pause:
            return "pause";
        case app::HostCommand::Toggle:
            return "toggle";
        case app::HostCommand::Previous:
            return "previous";
        case app::HostCommand::Next:
            return "next";
        case app::HostCommand::VolumeUp:
            return "volume_up";
        case app::HostCommand::VolumeDown:
            return "volume_down";
        case app::HostCommand::SetVolume:
            return "set_volume";
        case app::HostCommand::Mute:
            return "mute";
        case app::HostCommand::Seek:
            return "seek";
        case app::HostCommand::ShuffleToggle:
            return "shuffle_toggle";
        case app::HostCommand::SetRepeat:
            return "set_repeat";
        case app::HostCommand::Refresh:
            return "refresh";
        case app::HostCommand::SelectPlayer:
            return "select_player";
        case app::HostCommand::ListPlayers:
            return "list_players";
    }
    return "refresh";
}

void NetworkManager::sendCommand(const app::ControlRequest& request) {
    JsonDocument document;
    document["protocol"] = 1;
    document["type"] =
        request.command == app::HostCommand::ListPlayers ? "list_players" : "control";
    outgoingSequence_ = outgoingSequence_ >= kMaximumProtocolSequence ? 0 : outgoingSequence_ + 1;
    document["sequence"] = outgoingSequence_;
    document["timestamp_ms"] = millis();
    JsonObject payload = document["payload"].to<JsonObject>();
    if (request.command != app::HostCommand::ListPlayers) {
        payload["command"] = commandName(request.command);
    }
    if (request.command == app::HostCommand::SetVolume) {
        payload["value"] = std::clamp(request.decimalValue, 0.0F, 1.0F);
    } else if (request.command == app::HostCommand::Seek) {
        payload["offset_ms"] = std::clamp(request.integerValue, -1'800'000, 1'800'000);
    } else if (request.command == app::HostCommand::SetRepeat) {
        payload["mode"] = request.textValue;
    } else if (request.command == app::HostCommand::SelectPlayer) {
        payload["player_id"] = request.textValue;
    }
    String message;
    serializeJson(document, message);
    if (message.length() > 512 || !webSocket_.sendTXT(message)) {
        app::CommandFeedback feedback;
        feedback.requestSequence = outgoingSequence_;
        app::copyText(feedback.command, commandName(request.command));
        app::copyText(feedback.error, "Command could not be sent");
        xQueueSend(feedbackQueue_, &feedback, 0);
    }
}

void NetworkManager::processCommands() {
    app::ControlRequest request;
    std::uint8_t processed = 0;
    while (processed < 8 && xQueueReceive(commandQueue_, &request, 0) == pdTRUE) {
        if (webSocketConnected_) {
            sendCommand(request);
        } else {
            app::CommandFeedback feedback;
            app::copyText(feedback.command, commandName(request.command));
            app::copyText(feedback.error, "DeskWave Host is offline");
            xQueueSend(feedbackQueue_, &feedback, 0);
        }
        ++processed;
    }
}

void NetworkManager::rejectQueuedCommands(const char* reason) {
    app::ControlRequest request;
    while (xQueueReceive(commandQueue_, &request, 0) == pdTRUE) {
        app::CommandFeedback feedback;
        app::copyText(feedback.command, commandName(request.command));
        app::copyText(feedback.error, reason);
        xQueueSend(feedbackQueue_, &feedback, 0);
    }
}

}  // namespace deskwave::network
