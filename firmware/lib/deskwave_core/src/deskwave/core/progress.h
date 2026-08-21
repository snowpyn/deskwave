#pragma once

#include <cstdint>

namespace deskwave::core {

class ProgressClock {
  public:
    void synchronize(std::uint64_t positionMs, std::uint64_t durationMs, bool playing,
                     std::uint32_t localNowMs) noexcept;
    void seek(std::int64_t offsetMs, std::uint32_t localNowMs) noexcept;
    [[nodiscard]] std::uint64_t position(std::uint32_t localNowMs) const noexcept;
    [[nodiscard]] std::uint64_t duration() const noexcept;
    [[nodiscard]] float fraction(std::uint32_t localNowMs) const noexcept;

  private:
    std::uint64_t positionMs_{0};
    std::uint64_t durationMs_{0};
    std::uint32_t synchronizedAtMs_{0};
    bool playing_{false};
};

}  // namespace deskwave::core
