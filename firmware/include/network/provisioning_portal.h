#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "storage/settings_store.h"

namespace deskwave::network {

class ProvisioningPortal {
   public:
    explicit ProvisioningPortal(storage::SettingsStore& settingsStore);
    [[nodiscard]] bool begin(const String& deviceSuffix);
    void loop();
    void stop();
    [[nodiscard]] bool credentialsSaved() const noexcept;
    [[nodiscard]] const String& accessPointName() const noexcept;
    [[nodiscard]] const String& accessPointPassword() const noexcept;

   private:
    void handleRoot();
    void handleSave();
    void sendHeaders();
    static String randomText(std::size_t length);

    storage::SettingsStore& settingsStore_;
    WebServer server_{80};
    String accessPointName_;
    String accessPointPassword_;
    String nonce_;
    bool running_{false};
    bool credentialsSaved_{false};
    std::uint32_t stopAtMs_{0};
};

}  // namespace deskwave::network
