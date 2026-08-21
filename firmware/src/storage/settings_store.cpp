#include "storage/settings_store.h"

#include "config/build_config.h"
#include "system/logging.h"

namespace deskwave::storage {
namespace {

constexpr char kNamespace[] = "deskwave";

bool validSsid(const String& value) {
    return !value.isEmpty() && value.length() <= 32;
}

bool validPassword(const String& value) {
    return value.isEmpty() || (value.length() >= 8 && value.length() <= 63);
}

bool validToken(const String& value) {
    return value.length() >= 32 && value.length() <= 128;
}

bool validHost(const String& value) {
    return value.length() <= 253 && value.indexOf(' ') < 0;
}

}  // namespace

SettingsStore::SettingsStore() : mutex_(xSemaphoreCreateMutex()) {}

SettingsStore::~SettingsStore() {
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

bool SettingsStore::lock() {
    return mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(1'000)) == pdTRUE;
}

void SettingsStore::unlock() {
    xSemaphoreGive(mutex_);
}

bool SettingsStore::migrateLegacy(Preferences& preferences) {
    const String legacySsid = preferences.getString("wifi_ssid", "");
    const String legacyPassword = preferences.getString("wifi_pass", "");
    const String legacyToken = preferences.getString("host_token", "");
    if (!legacySsid.isEmpty() && validSsid(legacySsid) && validPassword(legacyPassword)) {
        preferences.putBool("wifi_valid", false);
        preferences.putString("ssid", legacySsid);
        preferences.putString("password", legacyPassword);
        preferences.putBool("wifi_valid", true);
    }
    if (!legacyToken.isEmpty() && validToken(legacyToken)) {
        preferences.putBool("paired", false);
        preferences.putString("token", legacyToken);
        preferences.putBool("paired", true);
    }
    return preferences.putUChar("schema", config::kSettingsSchemaVersion) == sizeof(std::uint8_t);
}

SettingsLoadStatus SettingsStore::load(DeviceSettings& settings) {
    if (!lock()) {
        return SettingsLoadStatus::Corrupt;
    }
    Preferences preferences;
    if (!preferences.begin(kNamespace, false)) {
        unlock();
        return SettingsLoadStatus::Corrupt;
    }
    bool migrated = false;
    if (!preferences.isKey("schema")) {
        if (preferences.isKey("wifi_ssid") || preferences.isKey("host_token")) {
            migrated = migrateLegacy(preferences);
            if (!migrated) {
                preferences.end();
                unlock();
                return SettingsLoadStatus::Corrupt;
            }
        } else {
            preferences.end();
            unlock();
            return SettingsLoadStatus::Empty;
        }
    }
    const auto schema = preferences.getUChar("schema", 0);
    if (schema > config::kSettingsSchemaVersion) {
        preferences.end();
        unlock();
        return SettingsLoadStatus::Unsupported;
    }
    if (schema < config::kSettingsSchemaVersion && !migrateLegacy(preferences)) {
        preferences.end();
        unlock();
        return SettingsLoadStatus::Corrupt;
    }

    settings = DeviceSettings{};
    settings.wifiConfigured = preferences.getBool("wifi_valid", false);
    settings.wifiSsid = preferences.getString("ssid", "");
    settings.wifiPassword = preferences.getString("password", "");
    settings.paired = preferences.getBool("paired", false);
    settings.hostToken = preferences.getString("token", "");
    settings.hostOverride = preferences.getString("host", "");
    settings.hostPort = preferences.getUShort("port", config::kDefaultHostPort);
    settings.brightness = preferences.getUChar("brightness", 180);
    settings.defaultScreen = preferences.getUChar("screen", 0);
    settings.volumeStepPercent =
        preferences.getUChar("volume_step", config::kDefaultVolumeStepPercent);
    settings.dimTimeoutSeconds = preferences.getULong("dim_seconds", 300);
    preferences.end();
    unlock();

    if ((settings.wifiConfigured &&
         (!validSsid(settings.wifiSsid) || !validPassword(settings.wifiPassword))) ||
        (settings.paired && !validToken(settings.hostToken)) ||
        !validHost(settings.hostOverride) || settings.hostPort == 0 || settings.brightness < 10 ||
        settings.defaultScreen > 3 || settings.volumeStepPercent < 1 ||
        settings.volumeStepPercent > 20 ||
        (settings.dimTimeoutSeconds != 0 &&
         (settings.dimTimeoutSeconds < 30 || settings.dimTimeoutSeconds > 86'400))) {
        settings = DeviceSettings{};
        return SettingsLoadStatus::Corrupt;
    }
    return migrated ? SettingsLoadStatus::Migrated : SettingsLoadStatus::Ok;
}

bool SettingsStore::saveWifi(const String& ssid, const String& password) {
    if (!validSsid(ssid) || !validPassword(password) || !lock()) {
        return false;
    }
    Preferences preferences;
    bool success = preferences.begin(kNamespace, false);
    if (success) {
        preferences.putUChar("schema", config::kSettingsSchemaVersion);
        preferences.putBool("wifi_valid", false);
        success = preferences.putString("ssid", ssid) == ssid.length() &&
                  preferences.putString("password", password) == password.length() &&
                  preferences.putBool("wifi_valid", true) == sizeof(bool);
        preferences.end();
    }
    unlock();
    return success;
}

bool SettingsStore::saveToken(const String& token) {
    if (!validToken(token) || !lock()) {
        return false;
    }
    Preferences preferences;
    bool success = preferences.begin(kNamespace, false);
    if (success) {
        preferences.putUChar("schema", config::kSettingsSchemaVersion);
        preferences.putBool("paired", false);
        success = preferences.putString("token", token) == token.length() &&
                  preferences.putBool("paired", true) == sizeof(bool);
        preferences.end();
    }
    unlock();
    return success;
}

bool SettingsStore::clearToken() {
    if (!lock()) {
        return false;
    }
    Preferences preferences;
    bool success = preferences.begin(kNamespace, false);
    if (success) {
        preferences.putBool("paired", false);
        success = preferences.remove("token");
        preferences.end();
    }
    unlock();
    return success;
}

bool SettingsStore::saveDisplay(const std::uint8_t brightness, const std::uint8_t defaultScreen,
                                const std::uint32_t dimTimeoutSeconds,
                                const std::uint8_t volumeStepPercent) {
    if (brightness < 10 || defaultScreen > 3 || volumeStepPercent < 1 ||
        volumeStepPercent > 20 ||
        (dimTimeoutSeconds != 0 && (dimTimeoutSeconds < 30 || dimTimeoutSeconds > 86'400)) ||
        !lock()) {
        return false;
    }
    Preferences preferences;
    bool success = preferences.begin(kNamespace, false);
    if (success) {
        preferences.putUChar("schema", config::kSettingsSchemaVersion);
        success = preferences.putUChar("brightness", brightness) == sizeof(brightness) &&
                  preferences.putUChar("screen", defaultScreen) == sizeof(defaultScreen) &&
                  preferences.putUChar("volume_step", volumeStepPercent) ==
                      sizeof(volumeStepPercent) &&
                  preferences.putULong("dim_seconds", dimTimeoutSeconds) ==
                      sizeof(dimTimeoutSeconds);
        preferences.end();
    }
    unlock();
    return success;
}

bool SettingsStore::saveHostOverride(const String& host, const std::uint16_t port) {
    if (!validHost(host) || port == 0 || !lock()) {
        return false;
    }
    Preferences preferences;
    bool success = preferences.begin(kNamespace, false);
    if (success) {
        preferences.putUChar("schema", config::kSettingsSchemaVersion);
        success = preferences.putString("host", host) == host.length() &&
                  preferences.putUShort("port", port) == sizeof(port);
        preferences.end();
    }
    unlock();
    return success;
}

bool SettingsStore::factoryReset() {
    if (!lock()) {
        return false;
    }
    Preferences preferences;
    const bool opened = preferences.begin(kNamespace, false);
    const bool success = opened && preferences.clear();
    if (opened) {
        preferences.end();
    }
    unlock();
    return success;
}

}  // namespace deskwave::storage
