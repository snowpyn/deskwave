#include "network/provisioning_portal.h"

#include <WiFi.h>
#include <esp_system.h>

#include "system/logging.h"

namespace deskwave::network {
namespace {

constexpr char kCharacters[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";

}  // namespace

ProvisioningPortal::ProvisioningPortal(storage::SettingsStore& settingsStore)
    : settingsStore_(settingsStore) {}

String ProvisioningPortal::randomText(const std::size_t length) {
    String value;
    value.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        value += kCharacters[esp_random() % (sizeof(kCharacters) - 1)];
    }
    return value;
}

bool ProvisioningPortal::begin(const String& deviceSuffix) {
    if (running_) {
        return true;
    }
    accessPointName_ = "DeskWave-" + deviceSuffix;
    accessPointPassword_ = randomText(12);
    nonce_ = randomText(24);
    credentialsSaved_ = false;
    stopAtMs_ = 0;
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(accessPointName_.c_str(), accessPointPassword_.c_str())) {
        DW_LOG_ERROR("provisioning", "Could not start the temporary access point");
        return false;
    }
    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/save", HTTP_POST, [this]() { handleSave(); });
    server_.onNotFound([this]() {
        sendHeaders();
        server_.send(404, "text/plain", "Not found");
    });
    server_.begin();
    running_ = true;
    DW_LOG_INFO("provisioning", "Temporary access point started");
    return true;
}

void ProvisioningPortal::sendHeaders() {
    server_.sendHeader("Cache-Control", "no-store");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    server_.sendHeader("Content-Security-Policy",
                       "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'");
}

void ProvisioningPortal::handleRoot() {
    sendHeaders();
    String page =
        F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
          "<title>DeskWave setup</title><style>body{font:18px system-ui;max-width:32rem;margin:"
          "3rem auto;padding:1rem;background:#111;color:#eee}label{display:block;margin-top:1rem}"
          "input,button{box-sizing:border-box;width:100%;padding:.8rem;margin-top:.35rem}"
          "button{background:#6e5cff;color:white;border:0;border-radius:.5rem}</style>"
          "<h1>DeskWave Wi-Fi</h1><p>Enter the network this device should join.</p>"
          "<form method=post action=/save><label>Network name<input name=ssid maxlength=32 "
          "required></label><label>Password<input name=password type=password maxlength=63></label>"
          "<input type=hidden name=nonce value='");
    page += nonce_;
    page += F("'><button type=submit>Save and connect</button></form>");
    server_.send(200, "text/html; charset=utf-8", page);
}

void ProvisioningPortal::handleSave() {
    sendHeaders();
    const String ssid = server_.arg("ssid");
    const String password = server_.arg("password");
    const String nonce = server_.arg("nonce");
    if (nonce != nonce_ || ssid.isEmpty() || ssid.length() > 32 ||
        (!password.isEmpty() && (password.length() < 8 || password.length() > 63))) {
        server_.send(400, "text/plain", "Invalid network details. Return and try again.");
        return;
    }
    if (!settingsStore_.saveWifi(ssid, password)) {
        server_.send(500, "text/plain", "DeskWave could not store the credentials safely.");
        return;
    }
    credentialsSaved_ = true;
    stopAtMs_ = millis() + 2'000;
    server_.send(200, "text/html; charset=utf-8",
                 "<!doctype html><title>DeskWave</title><h1>Saved</h1>"
                 "<p>DeskWave is connecting. You may close this page.</p>");
}

void ProvisioningPortal::loop() {
    if (!running_) {
        return;
    }
    server_.handleClient();
    if (stopAtMs_ != 0 && static_cast<std::int32_t>(millis() - stopAtMs_) >= 0) {
        stop();
    }
}

void ProvisioningPortal::stop() {
    if (!running_) {
        return;
    }
    server_.stop();
    WiFi.softAPdisconnect(true);
    running_ = false;
    DW_LOG_INFO("provisioning", "Temporary access point stopped");
}

bool ProvisioningPortal::credentialsSaved() const noexcept { return credentialsSaved_; }

const String& ProvisioningPortal::accessPointName() const noexcept { return accessPointName_; }

const String& ProvisioningPortal::accessPointPassword() const noexcept {
    return accessPointPassword_;
}

}  // namespace deskwave::network
