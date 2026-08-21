#pragma once

#include <cstddef>
#include <cstdint>

namespace deskwave::config {

inline constexpr std::uint8_t kSettingsSchemaVersion = 1;
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

}  // namespace deskwave::config
