#pragma once

#include <atomic>
#include <chrono>
#include <memory>

namespace switch_auto::base {

class Deadline {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    [[nodiscard]] static constexpr Deadline infinite() noexcept {
        return Deadline{};
    }

    [[nodiscard]] static Deadline at(TimePoint time) noexcept {
        return Deadline{time, false};
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static Deadline after(
        std::chrono::duration<Rep, Period> duration) noexcept {
        return at(Clock::now() +
                  std::chrono::duration_cast<Clock::duration>(duration));
    }

    [[nodiscard]] constexpr bool isInfinite() const noexcept { return infinite_; }
    [[nodiscard]] bool hasExpired(TimePoint now = Clock::now()) const noexcept {
        return !infinite_ && now >= time_;
    }
    [[nodiscard]] constexpr TimePoint timePoint() const noexcept { return time_; }

private:
    constexpr Deadline() noexcept = default;
    constexpr Deadline(TimePoint time, bool infinite) noexcept
        : time_(time), infinite_(infinite) {}

    TimePoint time_{};
    bool infinite_ = true;
};

class CancellationToken {
public:
    CancellationToken() noexcept = default;

    [[nodiscard]] bool isCancellationRequested() const noexcept {
        return state_ != nullptr && state_->load(std::memory_order_acquire);
    }

    [[nodiscard]] bool canBeCancelled() const noexcept {
        return state_ != nullptr;
    }

private:
    friend class CancellationSource;
    explicit CancellationToken(
        std::shared_ptr<const std::atomic<bool>> state) noexcept
        : state_(std::move(state)) {}

    // Shared ownership allows transports to retain a token until a blocking
    // operation has joined. Copying a token does not allocate; the single
    // allocation happens when a CancellationSource is created off the hot path.
    std::shared_ptr<const std::atomic<bool>> state_;
};

class CancellationSource {
public:
    CancellationSource()
        : cancelled_(std::make_shared<std::atomic<bool>>(false)) {}
    CancellationSource(const CancellationSource&) = delete;
    CancellationSource& operator=(const CancellationSource&) = delete;
    CancellationSource(CancellationSource&&) noexcept = default;
    CancellationSource& operator=(CancellationSource&&) noexcept = default;

    [[nodiscard]] CancellationToken token() const& noexcept {
        return CancellationToken{cancelled_};
    }
    [[nodiscard]] CancellationToken token() && = delete;

    void cancel() noexcept {
        if (cancelled_ != nullptr) {
            cancelled_->store(true, std::memory_order_release);
        }
    }

private:
    std::shared_ptr<std::atomic<bool>> cancelled_;
};

}  // namespace switch_auto::base
