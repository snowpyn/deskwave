#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <cstdint>

namespace deskwave::storage {

enum class SettingsLoadStatus : std::uint8_t { Ok, Empty, Migrated, Corrupt, Unsupported };

struct DeviceSettings {
    String wifiSsid;
    String wifiPassword;
    String hostToken;
    String hostOverride;
    std::uint16_t hostPort{8765};
    std::uint8_t brightness{180};
    std::uint8_t defaultScreen{0};
    std::uint32_t dimTimeoutSeconds{300};
    bool wifiConfigured{false};
    bool paired{false};
};

class SettingsStore {
  public:
    SettingsStore();
    ~SettingsStore();
    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    [[nodiscard]] SettingsLoadStatus load(DeviceSettings& settings);
    [[nodiscard]] bool saveWifi(const String& ssid, const String& password);
    [[nodiscard]] bool saveToken(const String& token);
    [[nodiscard]] bool saveDisplay(std::uint8_t brightness, std::uint8_t defaultScreen,
                                   std::uint32_t dimTimeoutSeconds);
    [[nodiscard]] bool saveHostOverride(const String& host, std::uint16_t port);
    [[nodiscard]] bool factoryReset();

  private:
    [[nodiscard]] bool lock();
    void unlock();
    [[nodiscard]] bool migrateLegacy(Preferences& preferences);

    SemaphoreHandle_t mutex_{nullptr};
};

}  // namespace deskwave::storage
