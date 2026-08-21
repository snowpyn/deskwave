#include "deskwave/core/progress.h"

#include <algorithm>

namespace deskwave::core {

void ProgressClock::synchronize(const std::uint64_t positionMs, const std::uint64_t durationMs,
                                const bool playing, const std::uint32_t localNowMs) noexcept {
    durationMs_ = durationMs;
    positionMs_ = durationMs_ == 0 ? positionMs : std::min(positionMs, durationMs_);
    playing_ = playing;
    synchronizedAtMs_ = localNowMs;
}

std::uint64_t ProgressClock::position(const std::uint32_t localNowMs) const noexcept {
    std::uint64_t result = positionMs_;
    if (playing_) {
        result += static_cast<std::uint32_t>(localNowMs - synchronizedAtMs_);
    }
    return durationMs_ == 0 ? result : std::min(result, durationMs_);
}

void ProgressClock::seek(const std::int64_t offsetMs, const std::uint32_t localNowMs) noexcept {
    const auto current = position(localNowMs);
    std::uint64_t target = 0;
    if (offsetMs >= 0) {
        target = current + static_cast<std::uint64_t>(offsetMs);
        if (durationMs_ != 0) {
            target = std::min(target, durationMs_);
        }
    } else {
        const auto magnitude = static_cast<std::uint64_t>(-(offsetMs + 1)) + 1;
        target = magnitude > current ? 0 : current - magnitude;
    }
    positionMs_ = target;
    synchronizedAtMs_ = localNowMs;
}

std::uint64_t ProgressClock::duration() const noexcept { return durationMs_; }

float ProgressClock::fraction(const std::uint32_t localNowMs) const noexcept {
    if (durationMs_ == 0) {
        return 0.0F;
    }
    return static_cast<float>(position(localNowMs)) / static_cast<float>(durationMs_);
}

}  // namespace deskwave::core
