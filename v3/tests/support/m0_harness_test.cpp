#include "core/base/byte_span.hpp"
#include "core/base/cancellation.hpp"
#include "core/base/errors.hpp"
#include "tests/fixtures/synthetic_streams.hpp"
#include "tests/support/fake_clock.hpp"
#include "tests/support/fake_duplex_transport.hpp"
#include "tests/support/test_runner.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace base = switch_auto::base;
namespace test = switch_auto::test;
namespace fixtures = switch_auto::test::fixtures;

namespace {

base::Deadline in(test::FakeClock& clock, std::chrono::milliseconds duration) {
    return base::Deadline::at(clock.now() + duration);
}

base::CancellationToken noCancellation() {
    return {};
}

void fakeClockControlsDeadlines(test::TestContext& context) {
    test::FakeClock clock;
    const auto deadline = in(clock, std::chrono::milliseconds{10});
    SA_TEST_EXPECT(context, !clock.hasExpired(deadline));
    clock.advance(std::chrono::milliseconds{9});
    SA_TEST_EXPECT(context, !clock.hasExpired(deadline));
    clock.advance(std::chrono::milliseconds{1});
    SA_TEST_EXPECT(context, clock.hasExpired(deadline));
}

void duplexPreservesBytesAcrossFragmentation(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    pair.first.writeFaults().setMaxChunk(3);
    pair.second.readFaults().setMaxChunk(2);

    std::size_t sent = 0;
    while (sent < fixtures::ascending_bytes.size()) {
        const base::ConstByteSpan remaining{
            fixtures::ascending_bytes.data() + sent,
            fixtures::ascending_bytes.size() - sent};
        auto result = pair.first.writeSome(
            remaining, in(clock, std::chrono::seconds{1}), noCancellation());
        SA_TEST_EXPECT(context, result.hasValue());
        if (!result) {
            return;
        }
        sent += *result.valueIf();
    }

    std::array<std::uint8_t, fixtures::ascending_bytes.size()> received{};
    std::size_t read = 0;
    while (read < received.size()) {
        auto result = pair.second.read(
            base::ByteSpan{received.data() + read, received.size() - read},
            in(clock, std::chrono::seconds{1}), noCancellation());
        SA_TEST_EXPECT(context, result.hasValue());
        if (!result) {
            return;
        }
        SA_TEST_EXPECT(context,
                       result.valueIf()->status == test::FakeReadStatus::data);
        SA_TEST_EXPECT(context, result.valueIf()->bytes_read <= 2);
        read += result.valueIf()->bytes_read;
    }

    SA_TEST_EXPECT(context, received == fixtures::ascending_bytes);
    SA_TEST_EXPECT(context, pair.first.stats().bytes_written == received.size());
    SA_TEST_EXPECT(context, pair.second.stats().bytes_read == received.size());
}

void duplexIsIndependentInBothDirections(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    const base::ConstByteSpan first_bytes{fixtures::ascending_bytes.data(), 3};
    const base::ConstByteSpan second_bytes{fixtures::alternating_bytes.data(), 4};
    SA_TEST_EXPECT(context,
                   pair.first.writeSome(first_bytes, base::Deadline::infinite(),
                                        noCancellation()).hasValue());
    SA_TEST_EXPECT(context,
                   pair.second.writeSome(second_bytes, base::Deadline::infinite(),
                                         noCancellation()).hasValue());

    std::array<std::uint8_t, 4> at_first{};
    std::array<std::uint8_t, 3> at_second{};
    auto first_read = pair.first.read(at_first, base::Deadline::infinite(),
                                      noCancellation());
    auto second_read = pair.second.read(at_second, base::Deadline::infinite(),
                                        noCancellation());
    SA_TEST_EXPECT(context, first_read.hasValue());
    SA_TEST_EXPECT(context, second_read.hasValue());
    SA_TEST_EXPECT(context, at_first[0] == 0xAA && at_first[1] == 0x55);
    SA_TEST_EXPECT(context, at_second[0] == 0x00 && at_second[2] == 0x02);
}

void timeoutAndCancellationAreDeterministic(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);

    std::uint8_t byte = 0;
    const auto deadline = in(clock, std::chrono::milliseconds{5});
    clock.advance(std::chrono::milliseconds{5});
    auto timed_out = pair.first.read({&byte, 1}, deadline, noCancellation());
    SA_TEST_EXPECT(context, !timed_out.hasValue());
    SA_TEST_EXPECT(context, timed_out.errorIf()->code ==
                                base::TransportErrorCode::timed_out);

    base::CancellationSource source;
    source.cancel();
    auto cancelled = pair.first.read({&byte, 1}, base::Deadline::infinite(),
                                     source.token());
    SA_TEST_EXPECT(context, !cancelled.hasValue());
    SA_TEST_EXPECT(context, cancelled.errorIf()->code ==
                                base::TransportErrorCode::cancelled);
}

void scriptedFaultAffectsOnlySelectedCall(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    pair.first.writeFaults().failCall(
        2, {base::TransportErrorCode::io_failure, 123});
    const base::ConstByteSpan bytes{fixtures::alternating_bytes.data(), 1};

    auto first = pair.first.writeSome(bytes, base::Deadline::infinite(),
                                      noCancellation());
    auto second = pair.first.writeSome(bytes, base::Deadline::infinite(),
                                       noCancellation());
    auto third = pair.first.writeSome(bytes, base::Deadline::infinite(),
                                      noCancellation());
    SA_TEST_EXPECT(context, first.hasValue());
    SA_TEST_EXPECT(context, !second.hasValue());
    SA_TEST_EXPECT(context,
                   second.errorIf()->code == base::TransportErrorCode::io_failure);
    SA_TEST_EXPECT(context, second.errorIf()->platform_code == 123);
    SA_TEST_EXPECT(context, third.hasValue());
    SA_TEST_EXPECT(context, pair.second.pendingBytes() == 2);
}

void halfCloseDrainsThenReportsEof(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    const base::ConstByteSpan bytes{fixtures::ascending_bytes.data(), 3};
    SA_TEST_EXPECT(context,
                   pair.first.writeSome(bytes, base::Deadline::infinite(),
                                        noCancellation()).hasValue());
    pair.first.closeWrite();

    std::array<std::uint8_t, 3> received{};
    auto data = pair.second.read(received, base::Deadline::infinite(),
                                 noCancellation());
    auto eof = pair.second.read(received, base::Deadline::infinite(),
                                noCancellation());
    SA_TEST_EXPECT(context, data.hasValue());
    SA_TEST_EXPECT(context, data.valueIf()->bytes_read == 3);
    SA_TEST_EXPECT(context, eof.hasValue());
    SA_TEST_EXPECT(context,
                   eof.valueIf()->status == test::FakeReadStatus::end_of_stream);

    // A write half-close does not close the opposite direction.
    const base::ConstByteSpan reply{fixtures::alternating_bytes.data(), 2};
    SA_TEST_EXPECT(context,
                   pair.second.writeSome(reply, base::Deadline::infinite(),
                                         noCancellation()).hasValue());
}

void closingReadRejectsPeerWrites(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    pair.second.closeRead();
    const base::ConstByteSpan bytes{fixtures::ascending_bytes.data(), 1};
    auto result = pair.first.writeSome(bytes, base::Deadline::infinite(),
                                       noCancellation());
    SA_TEST_EXPECT(context, !result.hasValue());
    SA_TEST_EXPECT(context, result.errorIf()->code ==
                                base::TransportErrorCode::connection_reset);
}

}  // namespace

int main() {
    return test::runTests({
        {"fake clock controls deadlines", fakeClockControlsDeadlines},
        {"duplex preserves bytes across fragmentation",
         duplexPreservesBytesAcrossFragmentation},
        {"duplex is independent in both directions",
         duplexIsIndependentInBothDirections},
        {"timeout and cancellation are deterministic",
         timeoutAndCancellationAreDeterministic},
        {"scripted fault affects only selected call",
         scriptedFaultAffectsOnlySelectedCall},
        {"half-close drains then reports EOF", halfCloseDrainsThenReportsEof},
        {"closing read rejects peer writes", closingReadRejectsPeerWrites},
    });
}
