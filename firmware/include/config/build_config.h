#pragma once

#include <cstddef>
#include <cstdint>

#if __has_include("config/device_secrets.h")
#include "config/device_secrets.h"
#endif

#ifndef DESKWAVE_BOOTSTRAP_WIFI_SSID
#define DESKWAVE_BOOTSTRAP_WIFI_SSID ""
#endif

#ifndef DESKWAVE_BOOTSTRAP_WIFI_PASSWORD
#define DESKWAVE_BOOTSTRAP_WIFI_PASSWORD ""
#endif

#ifndef DESKWAVE_FORCE_BOOTSTRAP_WIFI
#define DESKWAVE_FORCE_BOOTSTRAP_WIFI 0
#endif

namespace deskwave::config {

inline constexpr std::uint8_t kSettingsSchemaVersion = 2;
inline constexpr std::uint16_t kDefaultHostPort = 8765;
inline constexpr char kMdnsService[] = "_deskwave";
inline constexpr char kMdnsProtocol[] = "_tcp";
inline constexpr char kWebSocketPath[] = "/v1/ws";
inline constexpr std::size_t kMaximumProtocolMessageBytes = 8192;
inline constexpr std::size_t kMaximumArtworkBytes = 384 * 1024;
inline constexpr std::uint32_t kWifiAttemptTimeoutMs = 15'000;
inline constexpr std::uint32_t kHttpTimeoutMs = 4'000;
inline constexpr std::uint32_t kPairingPollMs = 5'000;
inline constexpr std::uint32_t kFactoryResetHoldMs = 5'000;
inline constexpr std::uint8_t kDefaultVolumeStepPercent = 5;
inline constexpr char kBootstrapWifiSsid[] = DESKWAVE_BOOTSTRAP_WIFI_SSID;
inline constexpr char kBootstrapWifiPassword[] = DESKWAVE_BOOTSTRAP_WIFI_PASSWORD;
inline constexpr bool kBootstrapWifiEnabled = kBootstrapWifiSsid[0] != '\0';
inline constexpr bool kForceBootstrapWifi = DESKWAVE_FORCE_BOOTSTRAP_WIFI != 0;

}  // namespace deskwave::config
