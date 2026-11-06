#pragma once

#include "core/base/cancellation.hpp"

#include <chrono>

namespace switch_auto::test {

class FakeClock {
public:
    using Clock = base::Deadline::Clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    constexpr FakeClock() noexcept = default;
    explicit constexpr FakeClock(TimePoint initial) noexcept : now_(initial) {}

    [[nodiscard]] constexpr TimePoint now() const noexcept { return now_; }

    constexpr void set(TimePoint value) noexcept { now_ = value; }

    template <typename Rep, typename Period>
    constexpr void advance(std::chrono::duration<Rep, Period> amount) noexcept {
        now_ += std::chrono::duration_cast<Duration>(amount);
    }

    [[nodiscard]] bool hasExpired(base::Deadline deadline) const noexcept {
        return deadline.hasExpired(now_);
    }

private:
    TimePoint now_{};
};

}  // namespace switch_auto::test
