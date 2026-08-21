#include "app/application.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "config/build_config.h"
#include "config/hardware_config.h"
#include "system/logging.h"

namespace deskwave::app {
namespace {

constexpr std::uint8_t kSettingsItemCount = 5;
constexpr std::uint32_t kSettingsWriteDelayMs = 1'500;
constexpr std::uint32_t kHealthRefreshMs = 2'000;
constexpr std::uint32_t kHealthLogMs = 60'000;
constexpr std::uint32_t kPlayerRefreshMs = 5'000;
constexpr std::int32_t kSeekStepMs = 10'000;

}  // namespace

Application::Application(storage::SettingsStore& settingsStore,
                         controls::InputManager& inputManager,
                         network::NetworkManager& networkManager,
                         network::ArtworkManager& artworkManager, ui::UiController& ui,
                         const QueueHandle_t inputQueue, const QueueHandle_t playbackQueue,
                         const QueueHandle_t noticeQueue, const QueueHandle_t commandQueue,
                         const QueueHandle_t feedbackQueue,
                         const QueueHandle_t artworkResultQueue,
                         const QueueHandle_t playerQueue)
    : settingsStore_(settingsStore),
      inputManager_(inputManager),
      networkManager_(networkManager),
      artworkManager_(artworkManager),
      ui_(ui),
      inputQueue_(inputQueue),
      playbackQueue_(playbackQueue),
      noticeQueue_(noticeQueue),
      commandQueue_(commandQueue),
      feedbackQueue_(feedbackQueue),
      artworkResultQueue_(artworkResultQueue),
      playerQueue_(playerQueue) {}

bool Application::begin() {
    if (begun_) {
        return true;
    }
    pinMode(hardware::kStatusLed, OUTPUT);
    digitalWrite(hardware::kStatusLed, LOW);

    storage::DeviceSettings loaded;
    const auto loadStatus = settingsStore_.load(loaded);
    settings_.brightness = loaded.brightness;
    settings_.defaultScreen = loaded.defaultScreen;
    settings_.dimTimeoutSeconds = loaded.dimTimeoutSeconds;
    settings_.volumeStepPercent = loaded.volumeStepPercent;
    loaded.wifiPassword.clear();
    loaded.hostToken.clear();

    const auto now = millis();
    if (!ui_.begin(settings_.brightness, settings_.defaultScreen, now)) {
        DW_LOG_ERROR("display", "Display initialization failed or returned an unexpected size");
        return false;
    }
    deviceStatus_.state = core::SystemState::Boot;
    copyText(deviceStatus_.stateLabel, "Starting DeskWave");
    ui_.setDeviceStatus(deviceStatus_);
    updateSettingsView();

    if (loadStatus == storage::SettingsLoadStatus::Corrupt ||
        loadStatus == storage::SettingsLoadStatus::Unsupported) {
        SystemNotice notice;
        notice.type = SystemNoticeType::StateChanged;
        notice.state = core::SystemState::Error;
        copyText(notice.primary, "Settings unavailable");
        copyText(notice.secondary, "Hold LEFT + RIGHT + MENU for 5 seconds to reset");
        ui_.setNotice(notice, now);
    }

    if (!inputManager_.begin()) {
        DW_LOG_ERROR("input", "Input task could not be created");
        return false;
    }
    if (!artworkManager_.begin()) {
        DW_LOG_ERROR("artwork", "Artwork task could not be created");
        return false;
    }
    if (!networkManager_.begin()) {
        DW_LOG_ERROR("network", "Network task could not be created");
        return false;
    }
    lastActivityAtMs_ = now;
    lastHealthUpdateMs_ = now - kHealthRefreshMs;
    begun_ = true;
    DW_LOG_INFO("system", "Application tasks started");
    return true;
}

bool Application::hostState(const core::SystemState state) noexcept {
    return state == core::SystemState::Ready || state == core::SystemState::Playing ||
           state == core::SystemState::Paused;
}

void Application::loop() {
    const auto now = millis();
    consumeQueues(now);
    if (!handleFactoryResetChord(now)) {
        consumeInput(now);
    }
    if (ui_.screen() == ui::Screen::Device && deviceStatus_.hostConnected &&
        static_cast<std::uint32_t>(now - lastPlayerRequestMs_) >= kPlayerRefreshMs) {
        requestPlayers(now);
    }
    persistSettingsIfDue(now);
    updateHealth(now);
    if (settings_.dimTimeoutSeconds != 0 &&
        static_cast<std::uint32_t>(now - lastActivityAtMs_) >=
            settings_.dimTimeoutSeconds * 1'000U) {
        if (!dimmed_) {
            dimmed_ = true;
            ui_.setDimmed(true, settings_.brightness);
        }
    }
    updateStatusLed(now);
    ui_.tick(now);
    vTaskDelay(pdMS_TO_TICKS(2));
}

void Application::consumeQueues(const std::uint32_t nowMs) {
    SystemNotice notice;
    while (xQueueReceive(noticeQueue_, &notice, 0) == pdTRUE) {
        deviceStatus_.state = notice.state;
        deviceStatus_.hostConnected = hostState(notice.state);
        if (notice.type == SystemNoticeType::NetworkDetails) {
            copyText(deviceStatus_.ipAddress, notice.primary);
            copyText(deviceStatus_.ssid, notice.secondary);
            deviceStatus_.rssi = notice.value;
            deviceStatus_.wifiConnected = true;
        } else if (notice.type == SystemNoticeType::StateChanged ||
                   notice.type == SystemNoticeType::PairingCode ||
                   notice.type == SystemNoticeType::ProvisioningStarted) {
            copyText(deviceStatus_.stateLabel, notice.primary);
            copyText(deviceStatus_.stateDetail, notice.secondary);
        }
        ui_.setNotice(notice, nowMs);
    }

    PlaybackSnapshot snapshot;
    if (xQueueReceive(playbackQueue_, &snapshot, 0) == pdTRUE) {
        playback_ = snapshot;
        hasPlayback_ = true;
        optimisticVolumePercent_ = snapshot.volumePercent;
        ui_.setPlayback(snapshot, nowMs);
    }

    ArtworkResult artwork;
    if (xQueueReceive(artworkResultQueue_, &artwork, 0) == pdTRUE) {
        ui_.setArtwork(artwork, nowMs);
    }

    CommandFeedback feedback;
    while (xQueueReceive(feedbackQueue_, &feedback, 0) == pdTRUE) {
        if (!feedback.success) {
            ui_.showToast(feedback.error[0] == '\0' ? "Media command failed" : feedback.error,
                          nowMs, true);
        }
    }

    PlayerListSnapshot playerList;
    if (xQueueReceive(playerQueue_, &playerList, 0) == pdTRUE) {
        players_ = playerList;
        selectedPlayer_ = 0;
        for (std::uint8_t index = 0; index < players_.count; ++index) {
            if (std::strcmp(players_.players[index].id, playback_.playerId) == 0) {
                selectedPlayer_ = index;
                break;
            }
        }
        ui_.setPlayers(players_, selectedPlayer_);
    }
}

void Application::consumeInput(const std::uint32_t nowMs) {
    controls::InputEvent event;
    std::uint8_t processed = 0;
    while (processed < 16 && xQueueReceive(inputQueue_, &event, 0) == pdTRUE) {
        lastActivityAtMs_ = nowMs;
        if (dimmed_) {
            dimmed_ = false;
            ui_.setDimmed(false, settings_.brightness);
        }
        handleInput(event, nowMs);
        ++processed;
    }
}

void Application::handleInput(const controls::InputEvent& event, const std::uint32_t nowMs) {
    const auto command = controlMapper_.map(ui_.controlContext(), event.control, event.gesture);
    handleCommand(command, nowMs);
}

bool Application::mediaControlAvailable(const std::uint32_t nowMs) {
    if (!deviceStatus_.hostConnected) {
        ui_.showToast("DeskWave Host is offline", nowMs, true);
        return false;
    }
    if (!hasPlayback_ || !playback_.canControl) {
        ui_.showToast("No controllable media player", nowMs, true);
        return false;
    }
    return true;
}

bool Application::sendRequest(const ControlRequest& request, const char* failureMessage,
                              const std::uint32_t nowMs) {
    if (xQueueSend(commandQueue_, &request, 0) == pdTRUE) {
        return true;
    }
    ui_.showToast(failureMessage, nowMs, true);
    DW_LOG_WARN("controls", "Host command queue is full");
    return false;
}

void Application::handleCommand(const core::ControlCommand command, const std::uint32_t nowMs) {
    ControlRequest request;
    switch (command) {
        case core::ControlCommand::None:
            return;
        case core::ControlCommand::NextScreen:
            ui_.nextScreen();
            settings_.factoryResetConfirmation = false;
            updateSettingsView();
            if (ui_.screen() == ui::Screen::Device) {
                requestPlayers(nowMs);
            }
            return;
        case core::ControlCommand::OpenActions:
            ui_.toggleActions();
            return;
        case core::ControlCommand::VolumeUp:
        case core::ControlCommand::VolumeDown: {
            if (!mediaControlAvailable(nowMs)) {
                return;
            }
            const auto direction = command == core::ControlCommand::VolumeUp ? 1 : -1;
            if (optimisticVolumePercent_ < 0) {
                request.command = direction > 0 ? HostCommand::VolumeUp : HostCommand::VolumeDown;
                if (!sendRequest(request, "Volume command queue is full", nowMs)) {
                    return;
                }
                return;
            }
            optimisticVolumePercent_ = static_cast<std::int16_t>(std::clamp<int>(
                optimisticVolumePercent_ + direction * settings_.volumeStepPercent, 0, 100));
            request.command = HostCommand::SetVolume;
            request.decimalValue = optimisticVolumePercent_ / 100.0F;
            if (sendRequest(request, "Volume command queue is full", nowMs)) {
                playback_.volumePercent = optimisticVolumePercent_;
                playback_.mutedKnown = true;
                playback_.muted = optimisticVolumePercent_ == 0;
                ui_.setPlayback(playback_, nowMs);
                ui_.showVolume(optimisticVolumePercent_, playback_.muted, nowMs);
            }
            return;
        }
        case core::ControlCommand::TogglePlayback:
            if (!mediaControlAvailable(nowMs)) {
                return;
            }
            request.command = HostCommand::Toggle;
            if (sendRequest(request, "Playback command queue is full", nowMs)) {
                const auto target = playback_.status == PlaybackStatus::Playing
                                        ? PlaybackStatus::Paused
                                        : PlaybackStatus::Playing;
                playback_.status = target;
                ui_.showTransport(target, nowMs);
            }
            return;
        case core::ControlCommand::Mute:
            if (!mediaControlAvailable(nowMs)) {
                return;
            }
            request.command = HostCommand::Mute;
            if (sendRequest(request, "Mute command queue is full", nowMs)) {
                playback_.mutedKnown = true;
                playback_.muted = !playback_.muted;
                ui_.setPlayback(playback_, nowMs);
                ui_.showVolume(std::max<std::int16_t>(0, optimisticVolumePercent_),
                               playback_.muted, nowMs);
            }
            return;
        case core::ControlCommand::Previous:
        case core::ControlCommand::Next:
            if (!mediaControlAvailable(nowMs)) {
                return;
            }
            if ((command == core::ControlCommand::Previous && !playback_.canPrevious) ||
                (command == core::ControlCommand::Next && !playback_.canNext)) {
                ui_.showToast("Track change is unavailable", nowMs, true);
                return;
            }
            request.command = command == core::ControlCommand::Previous ? HostCommand::Previous
                                                                         : HostCommand::Next;
            if (!sendRequest(request, "Track command queue is full", nowMs)) {
                return;
            }
            return;
        case core::ControlCommand::SeekBackward:
        case core::ControlCommand::SeekForward:
            if (!mediaControlAvailable(nowMs)) {
                return;
            }
            if (!playback_.canSeek) {
                ui_.showToast("Seeking is unavailable", nowMs, true);
                return;
            }
            request.command = HostCommand::Seek;
            request.integerValue = command == core::ControlCommand::SeekBackward ? -kSeekStepMs
                                                                                  : kSeekStepMs;
            if (sendRequest(request, "Seek command queue is full", nowMs)) {
                ui_.optimisticSeek(request.integerValue, nowMs);
            }
            return;
        case core::ControlCommand::ShuffleToggle:
            if (!mediaControlAvailable(nowMs)) {
                return;
            }
            if (!playback_.shuffleKnown) {
                ui_.showToast("Shuffle is unavailable", nowMs, true);
                return;
            }
            request.command = HostCommand::ShuffleToggle;
            if (sendRequest(request, "Shuffle command queue is full", nowMs)) {
                playback_.shuffle = !playback_.shuffle;
                ui_.setPlayback(playback_, nowMs);
            }
            return;
        case core::ControlCommand::CycleRepeat:
            if (!mediaControlAvailable(nowMs)) {
                return;
            }
            if (playback_.repeat == RepeatMode::Unknown) {
                ui_.showToast("Repeat is unavailable", nowMs, true);
                return;
            }
            request.command = HostCommand::SetRepeat;
            if (playback_.repeat == RepeatMode::Off) {
                copyText(request.textValue, "track");
                playback_.repeat = RepeatMode::Track;
            } else if (playback_.repeat == RepeatMode::Track) {
                copyText(request.textValue, "playlist");
                playback_.repeat = RepeatMode::Playlist;
            } else {
                copyText(request.textValue, "off");
                playback_.repeat = RepeatMode::Off;
            }
            if (sendRequest(request, "Repeat command queue is full", nowMs)) {
                ui_.setPlayback(playback_, nowMs);
            }
            return;
        case core::ControlCommand::BrightnessDown:
            adjustSetting(-1, nowMs);
            return;
        case core::ControlCommand::BrightnessUp:
            adjustSetting(1, nowMs);
            return;
        case core::ControlCommand::PreviousSetting:
            settings_.selectedItem = static_cast<std::uint8_t>(
                (settings_.selectedItem + kSettingsItemCount - 1) % kSettingsItemCount);
            settings_.factoryResetConfirmation = false;
            updateSettingsView();
            return;
        case core::ControlCommand::NextSetting:
            settings_.selectedItem =
                static_cast<std::uint8_t>((settings_.selectedItem + 1) % kSettingsItemCount);
            settings_.factoryResetConfirmation = false;
            updateSettingsView();
            return;
        case core::ControlCommand::ActivateSetting:
            activateSetting(nowMs);
            return;
        case core::ControlCommand::PreviousPlayer:
            selectRelativePlayer(-1);
            return;
        case core::ControlCommand::NextPlayer:
            selectRelativePlayer(1);
            return;
        case core::ControlCommand::SelectPlayer:
            selectPlayer(nowMs);
            return;
        case core::ControlCommand::RequestFactoryReset:
            if (settings_.factoryResetConfirmation) {
                performFactoryReset();
            } else {
                requestFactoryResetConfirmation(nowMs);
            }
            return;
    }
}

void Application::adjustSetting(const std::int8_t direction, const std::uint32_t nowMs) {
    settings_.factoryResetConfirmation = false;
    switch (settings_.selectedItem) {
        case 0: {
            const auto brightness = std::clamp<int>(settings_.brightness + direction * 10, 10, 255);
            settings_.brightness = static_cast<std::uint8_t>(brightness);
            ui_.setBrightness(settings_.brightness);
            dimmed_ = false;
            break;
        }
        case 1: {
            constexpr std::array<std::uint32_t, 7> options{0, 30, 60, 300, 900, 1'800, 3'600};
            std::size_t index = 0;
            std::uint32_t bestDistance = UINT32_MAX;
            for (std::size_t candidate = 0; candidate < options.size(); ++candidate) {
                const auto distance = options[candidate] > settings_.dimTimeoutSeconds
                                          ? options[candidate] - settings_.dimTimeoutSeconds
                                          : settings_.dimTimeoutSeconds - options[candidate];
                if (distance < bestDistance) {
                    bestDistance = distance;
                    index = candidate;
                }
            }
            if (direction > 0 && index + 1 < options.size()) {
                ++index;
            } else if (direction < 0 && index > 0) {
                --index;
            }
            settings_.dimTimeoutSeconds = options[index];
            break;
        }
        case 2:
            settings_.volumeStepPercent = static_cast<std::uint8_t>(std::clamp<int>(
                settings_.volumeStepPercent + direction, 1, 20));
            break;
        case 3:
            settings_.defaultScreen = static_cast<std::uint8_t>(
                (settings_.defaultScreen + 4 + direction) % 4);
            break;
        case 4:
            requestFactoryResetConfirmation(nowMs);
            return;
        default:
            return;
    }
    settingsDirty_ = true;
    settingsChangedAtMs_ = nowMs;
    updateSettingsView();
}

void Application::activateSetting(const std::uint32_t nowMs) {
    if (settings_.selectedItem == 4) {
        requestFactoryResetConfirmation(nowMs);
    } else {
        adjustSetting(1, nowMs);
    }
}

void Application::requestFactoryResetConfirmation(const std::uint32_t nowMs) {
    settings_.factoryResetConfirmation = true;
    updateSettingsView();
    ui_.showToast("Hold the encoder knob to confirm reset", nowMs, false);
}

void Application::updateSettingsView() {
    ui_.setSettings(settings_);
}

void Application::requestPlayers(const std::uint32_t nowMs) {
    if (!deviceStatus_.hostConnected) {
        return;
    }
    ControlRequest request;
    request.command = HostCommand::ListPlayers;
    if (sendRequest(request, "Player list request queue is full", nowMs)) {
        lastPlayerRequestMs_ = nowMs;
    }
}

void Application::selectRelativePlayer(const std::int8_t direction) {
    if (players_.count == 0) {
        return;
    }
    selectedPlayer_ = static_cast<std::uint8_t>(
        (selectedPlayer_ + players_.count + direction) % players_.count);
    ui_.setSelectedPlayer(selectedPlayer_);
}

void Application::selectPlayer(const std::uint32_t nowMs) {
    if (!deviceStatus_.hostConnected || players_.count == 0 ||
        selectedPlayer_ >= players_.count) {
        ui_.showToast("No playback device is available", nowMs, true);
        return;
    }
    ControlRequest request;
    request.command = HostCommand::SelectPlayer;
    copyText(request.textValue, players_.players[selectedPlayer_].id);
    if (sendRequest(request, "Player selection queue is full", nowMs)) {
        ui_.showToast("Playback device selected", nowMs, false);
    }
}

bool Application::handleFactoryResetChord(const std::uint32_t nowMs) {
    if (!inputManager_.factoryResetChordActive()) {
        if (factoryResetChordTiming_) {
            factoryResetChordTiming_ = false;
            ui_.hideFactoryResetChord();
        }
        return false;
    }
    controls::InputEvent discarded;
    while (xQueueReceive(inputQueue_, &discarded, 0) == pdTRUE) {
    }
    if (!factoryResetChordTiming_) {
        factoryResetChordTiming_ = true;
        factoryResetChordStartedAtMs_ = nowMs;
    }
    const auto elapsed = static_cast<std::uint32_t>(nowMs - factoryResetChordStartedAtMs_);
    if (elapsed >= 400) {
        const auto remaining = elapsed >= config::kFactoryResetHoldMs
                                   ? 0U
                                   : (config::kFactoryResetHoldMs - elapsed + 999U) / 1'000U;
        ui_.showFactoryResetChord(static_cast<std::uint8_t>(remaining));
    }
    if (elapsed >= config::kFactoryResetHoldMs) {
        performFactoryReset();
    }
    return true;
}

void Application::performFactoryReset() {
    ui_.showResetting();
    digitalWrite(hardware::kStatusLed, HIGH);
    if (!settingsStore_.factoryReset()) {
        ui_.showToast("Factory reset failed; settings were not changed", millis(), true);
        factoryResetChordTiming_ = false;
        settings_.factoryResetConfirmation = false;
        return;
    }
    WiFi.disconnect(true, true);
    DW_LOG_INFO("storage", "Factory reset completed; restarting");
    vTaskDelay(pdMS_TO_TICKS(350));
    ESP.restart();
}

void Application::persistSettingsIfDue(const std::uint32_t nowMs) {
    if (!settingsDirty_ ||
        static_cast<std::uint32_t>(nowMs - settingsChangedAtMs_) < kSettingsWriteDelayMs) {
        return;
    }
    if (settingsStore_.saveDisplay(settings_.brightness, settings_.defaultScreen,
                                   settings_.dimTimeoutSeconds,
                                   settings_.volumeStepPercent)) {
        settingsDirty_ = false;
        DW_LOG_INFO("storage", "Display and control preferences saved");
    } else {
        settingsChangedAtMs_ = nowMs;
        ui_.showToast("Settings could not be saved", nowMs, true);
        DW_LOG_WARN("storage", "Display preferences write failed; retrying later");
    }
}

void Application::updateHealth(const std::uint32_t nowMs) {
    if (static_cast<std::uint32_t>(nowMs - lastHealthUpdateMs_) < kHealthRefreshMs) {
        return;
    }
    lastHealthUpdateMs_ = nowMs;
    deviceStatus_.wifiConnected = WiFi.status() == WL_CONNECTED;
    if (deviceStatus_.wifiConnected) {
        copyText(deviceStatus_.ssid, WiFi.SSID().c_str());
        copyText(deviceStatus_.ipAddress, WiFi.localIP().toString().c_str());
        deviceStatus_.rssi = WiFi.RSSI();
    } else {
        deviceStatus_.ssid[0] = '\0';
        deviceStatus_.ipAddress[0] = '\0';
        deviceStatus_.rssi = 0;
    }
    deviceStatus_.hostConnected = hostState(deviceStatus_.state);
    deviceStatus_.uptimeSeconds = nowMs / 1'000U;
    deviceStatus_.freeHeap = ESP.getFreeHeap();
    deviceStatus_.largestFreeBlock =
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    ui_.setDeviceStatus(deviceStatus_);

    if (static_cast<std::uint32_t>(nowMs - lastHealthLogMs_) >= kHealthLogMs) {
        lastHealthLogMs_ = nowMs;
        DW_LOG_INFO("health", "heap=%lu largest=%lu state=%u queues=%u/%u/%u",
                    static_cast<unsigned long>(deviceStatus_.freeHeap),
                    static_cast<unsigned long>(deviceStatus_.largestFreeBlock),
                    static_cast<unsigned>(deviceStatus_.state),
                    static_cast<unsigned>(uxQueueMessagesWaiting(inputQueue_)),
                    static_cast<unsigned>(uxQueueMessagesWaiting(commandQueue_)),
                    static_cast<unsigned>(uxQueueMessagesWaiting(noticeQueue_)));
    }
}

void Application::updateStatusLed(const std::uint32_t nowMs) {
    bool enabled = false;
    if (deviceStatus_.hostConnected) {
        enabled = true;
    } else if (deviceStatus_.state == core::SystemState::Error) {
        enabled = (nowMs / 150U) % 2U == 0;
    } else {
        enabled = (nowMs / 600U) % 4U == 0;
    }
    digitalWrite(hardware::kStatusLed, enabled ? HIGH : LOW);
}

}  // namespace deskwave::app
