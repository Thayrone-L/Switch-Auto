#pragma once

#include "core/base/byte_span.hpp"
#include "core/base/cancellation.hpp"
#include "core/base/errors.hpp"
#include "core/base/result.hpp"
#include "core/contracts/transport.hpp"
#include "tests/faults/io_fault_plan.hpp"
#include "tests/support/fake_clock.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <utility>

namespace switch_auto::test {

struct FakeTransportStats {
    std::size_t read_calls = 0;
    std::size_t write_calls = 0;
    std::size_t bytes_read = 0;
    std::size_t bytes_written = 0;
    std::size_t would_block = 0;
    std::size_t wakeups_observed = 0;
};

// Platform-independent in-memory duplex stream implementing ITransport.
class FakeDuplexEndpoint final : public contracts::ITransport {
public:
    using ReadOutcome =
        base::Result<contracts::ReadResult, base::TransportError>;
    using WriteOutcome = base::Result<std::size_t, base::TransportError>;

    FakeDuplexEndpoint(const FakeDuplexEndpoint&) = delete;
    FakeDuplexEndpoint& operator=(const FakeDuplexEndpoint&) = delete;
    FakeDuplexEndpoint(FakeDuplexEndpoint&&) noexcept = default;
    FakeDuplexEndpoint& operator=(FakeDuplexEndpoint&&) noexcept = default;

    [[nodiscard]] ReadOutcome read(base::ByteSpan target,
                                   base::Deadline deadline,
                                   base::CancellationToken cancellation) noexcept override {
        ++stats_.read_calls;
        // Keep this order: cancellation, deadline, empty target, fault,
        // local receive close, pending data, peer EOF, wakeup, would-block.
        if (cancellation.isCancellationRequested()) {
            return failure<contracts::ReadResult>(base::TransportErrorCode::cancelled);
        }
        if (clock_->hasExpired(deadline)) {
            return failure<contracts::ReadResult>(base::TransportErrorCode::timed_out);
        }
        if (target.empty()) {
            return ReadOutcome::success({0, contracts::ReadStatus::data});
        }
        if (const auto fault = read_faults_.faultFor(stats_.read_calls)) {
            return ReadOutcome::failure(*fault);
        }
        if (state_->read_closed[index_]) {
            return ReadOutcome::success(
                {0, contracts::ReadStatus::end_of_stream});
        }

        auto& incoming = state_->pending[index_];
        if (!incoming.empty()) {
            const auto count = std::min(
                target.size(), std::min(read_faults_.limit(target.size()),
                                        incoming.size()));
            for (std::size_t i = 0; i < count; ++i) {
                target[i] = incoming.front();
                incoming.pop_front();
            }
            stats_.bytes_read += count;
            return ReadOutcome::success({count, contracts::ReadStatus::data});
        }
        if (state_->write_closed[peerIndex()]) {
            return ReadOutcome::success(
                {0, contracts::ReadStatus::end_of_stream});
        }
        if (wakeup_pending_) {
            wakeup_pending_ = false;
            ++stats_.wakeups_observed;
            return ReadOutcome::success({0, contracts::ReadStatus::woken_up});
        }
        // The fake is single-thread and does not block: 'would-block' is reported as timed_out.
        ++stats_.would_block;
        return failure<contracts::ReadResult>(base::TransportErrorCode::timed_out);
    }

    [[nodiscard]] base::Result<void, base::TransportError> writeAll(
        base::ConstByteSpan bytes, base::Deadline deadline,
        base::CancellationToken cancellation) noexcept override {
        if (cancellation.isCancellationRequested()) {
            return failure<void>(base::TransportErrorCode::cancelled);
        }
        if (clock_->hasExpired(deadline)) {
            return failure<void>(base::TransportErrorCode::timed_out);
        }
        if (bytes.empty()) {
            return base::Result<void, base::TransportError>::success();
        }

        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto call = ++stats_.write_calls;
            if (const auto fault = write_faults_.faultFor(call)) {
                return base::Result<void, base::TransportError>::failure(*fault);
            }
            if (state_->write_closed[index_] || state_->read_closed[peerIndex()]) {
                return failure<void>(base::TransportErrorCode::connection_reset);
            }
            const auto count = write_faults_.limit(bytes.size() - offset);
            auto& outgoing = state_->pending[peerIndex()];
            for (std::size_t i = 0; i < count; ++i) {
                outgoing.push_back(bytes[offset + i]);
            }
            offset += count;
            stats_.bytes_written += count;
        }
        return base::Result<void, base::TransportError>::success();
    }

    [[nodiscard]] WriteOutcome writeSome(
        base::ConstByteSpan bytes, base::Deadline deadline,
        base::CancellationToken cancellation) noexcept {
        const auto call = ++stats_.write_calls;
        if (cancellation.isCancellationRequested()) {
            return failure<std::size_t>(base::TransportErrorCode::cancelled);
        }
        if (clock_->hasExpired(deadline)) {
            return failure<std::size_t>(base::TransportErrorCode::timed_out);
        }
        if (bytes.empty()) {
            return WriteOutcome::success(0);
        }
        if (const auto fault = write_faults_.faultFor(call)) {
            return WriteOutcome::failure(*fault);
        }
        if (state_->write_closed[index_] || state_->read_closed[peerIndex()]) {
            return failure<std::size_t>(base::TransportErrorCode::connection_reset);
        }
        const auto count = write_faults_.limit(bytes.size());
        auto& outgoing = state_->pending[peerIndex()];
        for (std::size_t i = 0; i < count; ++i) {
            outgoing.push_back(bytes[i]);
        }
        stats_.bytes_written += count;
        return WriteOutcome::success(count);
    }

    void wakeup() noexcept override { wakeup_pending_ = true; }

    [[nodiscard]] base::Result<void, base::TransportError> shutdown(
        contracts::ShutdownMode mode) noexcept override {
        if (mode == contracts::ShutdownMode::receive ||
            mode == contracts::ShutdownMode::both) {
            state_->read_closed[index_] = true;
        }
        if (mode == contracts::ShutdownMode::send ||
            mode == contracts::ShutdownMode::both) {
            state_->write_closed[index_] = true;
        }
        return base::Result<void, base::TransportError>::success();
    }

    [[nodiscard]] bool isWriteClosed() const noexcept {
        return state_->write_closed[index_];
    }
    [[nodiscard]] bool isReadClosed() const noexcept {
        return state_->read_closed[index_];
    }
    [[nodiscard]] std::size_t pendingBytes() const noexcept {
        return state_->pending[index_].size();
    }
    [[nodiscard]] IoFaultPlan& readFaults() noexcept { return read_faults_; }
    [[nodiscard]] IoFaultPlan& writeFaults() noexcept { return write_faults_; }
    [[nodiscard]] const FakeTransportStats& stats() const noexcept {
        return stats_;
    }

private:
    struct SharedState {
        std::array<std::deque<std::uint8_t>, 2> pending;
        std::array<bool, 2> read_closed{{false, false}};
        std::array<bool, 2> write_closed{{false, false}};
    };

    friend class FakeDuplexTransport;
    FakeDuplexEndpoint(std::shared_ptr<SharedState> state, std::size_t index,
                       FakeClock& clock) noexcept
        : state_(std::move(state)), index_(index), clock_(&clock) {}

    [[nodiscard]] constexpr std::size_t peerIndex() const noexcept {
        return index_ == 0 ? 1 : 0;
    }

    template <typename Value>
    [[nodiscard]] static base::Result<Value, base::TransportError> failure(
        base::TransportErrorCode code) noexcept {
        return base::Result<Value, base::TransportError>::failure({code, 0});
    }

    std::shared_ptr<SharedState> state_;
    std::size_t index_;
    FakeClock* clock_;
    IoFaultPlan read_faults_;
    IoFaultPlan write_faults_;
    FakeTransportStats stats_;
    bool wakeup_pending_ = false;
};

class FakeDuplexTransport {
public:
    struct Pair {
        FakeDuplexEndpoint first;
        FakeDuplexEndpoint second;
    };

    [[nodiscard]] static Pair create(FakeClock& clock) {
        auto state = std::make_shared<FakeDuplexEndpoint::SharedState>();
        return {FakeDuplexEndpoint{state, 0, clock},
                FakeDuplexEndpoint{std::move(state), 1, clock}};
    }
};

}  // namespace switch_auto::test
