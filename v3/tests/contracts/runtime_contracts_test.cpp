#include "core/contracts/input_source.hpp"
#include "core/contracts/metrics_sink.hpp"
#include "core/contracts/tls_engine.hpp"
#include "core/contracts/video.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace base = switch_auto::base;
namespace contracts = switch_auto::contracts;

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,     \
                         __LINE__, #condition);                                 \
            ++failures;                                                        \
        }                                                                       \
    } while (false)

class FakeTransport final : public contracts::ITransport {
public:
    using ReadOutcome =
        base::Result<contracts::ReadResult, base::TransportError>;

    base::Result<contracts::ReadResult, base::TransportError> read(
        base::ByteSpan target, base::Deadline,
        base::CancellationToken cancellation) noexcept override {
        if (cancellation.isCancellationRequested()) {
            return ReadOutcome::failure(
                {base::TransportErrorCode::cancelled, 0});
        }
        if (woken_) {
            woken_ = false;
            return ReadOutcome::success(
                {0, contracts::ReadStatus::woken_up});
        }
        if (offset_ == size_) {
            return ReadOutcome::success(
                {0, contracts::ReadStatus::end_of_stream});
        }
        if (target.empty()) {
            return ReadOutcome::success(
                {0, contracts::ReadStatus::data});
        }
        const auto remaining = size_ - offset_;
        const auto read_limit = remaining < 2 ? remaining : 2;
        const auto count = target.size() < read_limit ? target.size() : read_limit;
        std::memcpy(target.data(), data_ + offset_, count);
        offset_ += count;
        return ReadOutcome::success(
            {count, contracts::ReadStatus::data});
    }

    base::Result<void, base::TransportError> writeAll(
        base::ConstByteSpan bytes, base::Deadline,
        base::CancellationToken cancellation) noexcept override {
        if (cancellation.isCancellationRequested()) {
            return base::Result<void, base::TransportError>::failure(
                {base::TransportErrorCode::cancelled, 0});
        }
        bytes_written_ += bytes.size();
        return base::Result<void, base::TransportError>::success();
    }

    void wakeup() noexcept override { woken_ = true; }

    base::Result<void, base::TransportError> shutdown(
        contracts::ShutdownMode) noexcept override {
        return base::Result<void, base::TransportError>::success();
    }

    std::size_t bytes_written() const noexcept { return bytes_written_; }

private:
    const std::uint8_t data_[3]{1, 2, 3};
    std::size_t size_ = 3;
    std::size_t offset_ = 0;
    std::size_t bytes_written_ = 0;
    bool woken_ = false;
};

class FakeTlsEngine final : public contracts::ITlsEngine {
public:
    base::Result<void, contracts::TlsError> handshake(
        contracts::ITransport&, base::Deadline,
        base::CancellationToken) noexcept override {
        ready_ = true;
        return base::Result<void, contracts::TlsError>::success();
    }

    base::Result<contracts::TlsReadResult, contracts::TlsError> read(
        contracts::ITransport&, base::ByteSpan, base::Deadline,
        base::CancellationToken) noexcept override {
        return base::Result<contracts::TlsReadResult,
                            contracts::TlsError>::success(
            {0, contracts::TlsReadStatus::end_of_stream});
    }

    base::Result<void, contracts::TlsError> writeAll(
        contracts::ITransport&, base::ConstByteSpan, base::Deadline,
        base::CancellationToken) noexcept override {
        return ready_ ? base::Result<void, contracts::TlsError>::success()
                      : base::Result<void, contracts::TlsError>::failure(
                            {contracts::TlsErrorCode::invalid_state, 0, {}});
    }

    base::Result<void, contracts::TlsError> shutdown(
        contracts::ITransport&, base::Deadline,
        base::CancellationToken) noexcept override {
        ready_ = false;
        return base::Result<void, contracts::TlsError>::success();
    }

private:
    bool ready_ = false;
};

class FakeDecoder final : public contracts::IVideoDecoder {
public:
    using DecodeOutcome =
        base::Result<contracts::DecodedSurface, contracts::VideoError>;

    base::Result<contracts::DecodedSurface, contracts::VideoError> decode(
        const contracts::EncodedAccessUnit& unit, base::Deadline,
        base::CancellationToken) noexcept override {
        if (unit.bytes.empty()) {
            return DecodeOutcome::failure(
                {contracts::VideoErrorCode::invalid_data, 0});
        }
        return DecodeOutcome::success(
            {1, 1280, 720, 1280, contracts::PixelFormat::nv12,
             unit.presentation_time_us});
    }

    void release(contracts::DecodedSurface&& surface) noexcept override {
        released_ = surface.handle;
        surface.handle = 0;
    }

    std::uint64_t released() const noexcept { return released_; }

private:
    std::uint64_t released_ = 0;
};

class FakePresenter final : public contracts::IVideoPresenter {
public:
    base::Result<void, contracts::VideoError> present(
        const contracts::DecodedSurface& surface, base::Deadline,
        base::CancellationToken) noexcept override {
        last_ = surface.handle;
        return base::Result<void, contracts::VideoError>::success();
    }

    std::uint64_t last() const noexcept { return last_; }

private:
    std::uint64_t last_ = 0;
};

class FakeInput final : public contracts::IInputSource {
public:
    using InputOutcome =
        base::Result<contracts::InputReadResult, contracts::InputError>;

    base::Result<contracts::InputReadResult, contracts::InputError> read(
        base::Deadline,
        base::CancellationToken cancellation) noexcept override {
        if (cancellation.isCancellationRequested()) {
            return InputOutcome::failure(
                {contracts::InputErrorCode::cancelled, 0});
        }
        return InputOutcome::success(
            {contracts::InputReadStatus::event,
             {contracts::InputAction::down, 0, 10.0F, 20.0F, 30}});
    }
    void wakeup() noexcept override {}
};

class FakeMetrics final : public contracts::IMetricsSink {
public:
    void increment(contracts::MetricId, std::uint64_t amount) noexcept override {
        counter += amount;
    }
    void observe(contracts::MetricId, std::uint64_t value) noexcept override {
        observation = value;
    }
    std::uint64_t counter = 0;
    std::uint64_t observation = 0;
};

void transportContractIsImplementable() {
    FakeTransport transport;
    std::uint8_t storage[4]{};
    auto first = transport.read({storage, sizeof(storage)},
                                base::Deadline::infinite(), {});
    CHECK(first.valueIf() != nullptr);
    CHECK(first.valueIf()->bytes_read == 2);
    CHECK(first.valueIf()->status == contracts::ReadStatus::data);

    transport.wakeup();
    auto wake = transport.read({storage, sizeof(storage)},
                               base::Deadline::infinite(), {});
    CHECK(wake.valueIf() != nullptr);
    CHECK(wake.valueIf()->status == contracts::ReadStatus::woken_up);

    auto tail = transport.read({storage, sizeof(storage)},
                               base::Deadline::infinite(), {});
    CHECK(tail.valueIf() != nullptr);
    CHECK(tail.valueIf()->bytes_read == 1);
    auto eof = transport.read({storage, sizeof(storage)},
                              base::Deadline::infinite(), {});
    CHECK(eof.valueIf() != nullptr);
    CHECK(eof.valueIf()->status == contracts::ReadStatus::end_of_stream);

    const std::uint8_t outgoing[]{4, 5, 6};
    CHECK(transport.writeAll(outgoing, base::Deadline::infinite(), {}).hasValue());
    CHECK(transport.bytes_written() == 3);
}

void remainingContractsAreImplementable() {
    FakeTransport transport;
    FakeTlsEngine tls;
    CHECK(tls.handshake(transport, base::Deadline::infinite(), {}).hasValue());

    const std::uint8_t encoded[]{0, 0, 1, 9};
    FakeDecoder decoder;
    auto decoded = decoder.decode({encoded, 99, false, true},
                                  base::Deadline::infinite(), {});
    CHECK(decoded.valueIf() != nullptr);

    FakePresenter presenter;
    CHECK(presenter.present(*decoded.valueIf(), base::Deadline::infinite(), {})
              .hasValue());
    CHECK(presenter.last() == 1);
    decoder.release(std::move(*decoded.valueIf()));
    CHECK(decoder.released() == 1);
    CHECK(decoded.valueIf()->handle == 0);

    FakeInput input;
    auto event = input.read(base::Deadline::infinite(), {});
    CHECK(event.valueIf() != nullptr);
    CHECK(event.valueIf()->status == contracts::InputReadStatus::event);

    FakeMetrics metrics;
    metrics.increment(1, 2);
    metrics.observe(2, 7);
    CHECK(metrics.counter == 2);
    CHECK(metrics.observation == 7);
}

static_assert(std::has_virtual_destructor_v<contracts::ITransport>);
static_assert(std::has_virtual_destructor_v<contracts::ITlsEngine>);
static_assert(std::has_virtual_destructor_v<contracts::IVideoDecoder>);
static_assert(std::has_virtual_destructor_v<contracts::IVideoPresenter>);
static_assert(std::has_virtual_destructor_v<contracts::IInputSource>);
static_assert(std::has_virtual_destructor_v<contracts::IMetricsSink>);
static_assert(!std::is_copy_constructible_v<contracts::DecodedSurface>);
static_assert(std::is_nothrow_move_constructible_v<contracts::DecodedSurface>);

}  // namespace

int main() {
    transportContractIsImplementable();
    remainingContractsAreImplementable();
    return failures == 0 ? 0 : 1;
}
