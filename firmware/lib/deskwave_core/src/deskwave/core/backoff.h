#pragma once

#include <algorithm>
#include <cstdint>

namespace deskwave::core {

class ReconnectBackoff {
  public:
    constexpr ReconnectBackoff(std::uint32_t baseMs = 500, std::uint32_t maximumMs = 30000)
        : baseMs_(baseMs), maximumMs_(maximumMs) {}

    [[nodiscard]] std::uint32_t next(std::uint32_t randomValue) noexcept {
        const auto shift = std::min<std::uint8_t>(attempt_, 15);
        const auto expanded = static_cast<std::uint64_t>(baseMs_) << shift;
        const auto bounded = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(expanded, static_cast<std::uint64_t>(maximumMs_)));
        const auto jitterRange = std::max<std::uint32_t>(1, bounded / 4);
        if (attempt_ < 31) {
            ++attempt_;
        }
        return std::min(maximumMs_, bounded + (randomValue % jitterRange));
    }

    void reset() noexcept { attempt_ = 0; }
    [[nodiscard]] std::uint8_t attempts() const noexcept { return attempt_; }

  private:
    std::uint32_t baseMs_;
    std::uint32_t maximumMs_;
    std::uint8_t attempt_{0};
};

}  // namespace deskwave::core
