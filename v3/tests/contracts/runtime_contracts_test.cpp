#include "core/contracts/input_source.hpp"
#include "core/contracts/metrics_sink.hpp"
#include "core/contracts/tls_engine.hpp"
#include "core/contracts/video.hpp"
#include "tests/support/fake_duplex_transport.hpp"
#include "tests/support/test_runner.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace base = switch_auto::base;
namespace contracts = switch_auto::contracts;
namespace test = switch_auto::test;

namespace {

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
        return base::Result<contracts::TlsReadResult, contracts::TlsError>::success(
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
    using DecodeOutcome = base::Result<contracts::DecodedSurface, contracts::VideoError>;
    base::Result<contracts::DecodedSurface, contracts::VideoError> decode(
        const contracts::EncodedAccessUnit& unit, base::Deadline,
        base::CancellationToken) noexcept override {
        if (unit.bytes.empty()) {
            return DecodeOutcome::failure({contracts::VideoErrorCode::invalid_data, 0});
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
    using InputOutcome = base::Result<contracts::InputReadResult, contracts::InputError>;
    base::Result<contracts::InputReadResult, contracts::InputError> read(
        base::Deadline, base::CancellationToken cancellation) noexcept override {
        if (cancellation.isCancellationRequested()) {
            return InputOutcome::failure({contracts::InputErrorCode::cancelled, 0});
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

void transportContractIsImplementable(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    const std::uint8_t incoming[]{1, 2, 3};
    SA_TEST_EXPECT(context, pair.second.writeAll({incoming, sizeof(incoming)},
        base::Deadline::infinite(), {}).hasValue());
    std::uint8_t storage[4]{};
    auto first = pair.first.read({storage, sizeof(storage)}, base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, first.valueIf() != nullptr);
    SA_TEST_EXPECT(context, first.valueIf()->bytes_read == 3);
    SA_TEST_EXPECT(context, first.valueIf()->status == contracts::ReadStatus::data);

    pair.first.wakeup();
    auto wake = pair.first.read({storage, sizeof(storage)}, base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, wake.valueIf() != nullptr);
    SA_TEST_EXPECT(context, wake.valueIf()->status == contracts::ReadStatus::woken_up);
    SA_TEST_EXPECT(context, pair.second.shutdown(contracts::ShutdownMode::send).hasValue());
    auto eof = pair.first.read({storage, sizeof(storage)}, base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, eof.valueIf() != nullptr);
    SA_TEST_EXPECT(context, eof.valueIf()->status == contracts::ReadStatus::end_of_stream);

    const std::uint8_t outgoing[]{4, 5, 6};
    SA_TEST_EXPECT(context, pair.first.writeAll({outgoing, sizeof(outgoing)},
        base::Deadline::infinite(), {}).hasValue());
    SA_TEST_EXPECT(context, pair.first.stats().bytes_written == 3);
}

void remainingContractsAreImplementable(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    FakeTlsEngine tls;
    SA_TEST_EXPECT(context, tls.handshake(pair.first, base::Deadline::infinite(), {}).hasValue());

    const std::uint8_t encoded[]{0, 0, 1, 9};
    FakeDecoder decoder;
    auto decoded = decoder.decode({{encoded, sizeof(encoded)}, 99, false, true},
                                  base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, decoded.valueIf() != nullptr);
    FakePresenter presenter;
    SA_TEST_EXPECT(context, presenter.present(*decoded.valueIf(), base::Deadline::infinite(), {}).hasValue());
    SA_TEST_EXPECT(context, presenter.last() == 1);
    decoder.release(std::move(*decoded.valueIf()));
    SA_TEST_EXPECT(context, decoder.released() == 1);
    SA_TEST_EXPECT(context, decoded.valueIf()->handle == 0);

    FakeInput input;
    auto event = input.read(base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, event.valueIf() != nullptr);
    SA_TEST_EXPECT(context, event.valueIf()->status == contracts::InputReadStatus::event);
    FakeMetrics metrics;
    metrics.increment(1, 2);
    metrics.observe(2, 7);
    SA_TEST_EXPECT(context, metrics.counter == 2);
    SA_TEST_EXPECT(context, metrics.observation == 7);
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
    return test::runTests({
        {"transport contract is implementable", transportContractIsImplementable},
        {"remaining contracts are implementable", remainingContractsAreImplementable},
    });
}
