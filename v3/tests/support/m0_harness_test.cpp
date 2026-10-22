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
namespace contracts = switch_auto::contracts;
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
                       result.valueIf()->status == contracts::ReadStatus::data);
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
    auto first_read = pair.first.read(base::ByteSpan{at_first.data(), at_first.size()}, base::Deadline::infinite(),
                                      noCancellation());
    auto second_read = pair.second.read(base::ByteSpan{at_second.data(), at_second.size()}, base::Deadline::infinite(),
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
    SA_TEST_EXPECT(context, pair.first.shutdown(contracts::ShutdownMode::send).hasValue());

    std::array<std::uint8_t, 3> received{};
    auto data = pair.second.read(base::ByteSpan{received.data(), received.size()}, base::Deadline::infinite(),
                                 noCancellation());
    auto eof = pair.second.read(base::ByteSpan{received.data(), received.size()}, base::Deadline::infinite(),
                                noCancellation());
    SA_TEST_EXPECT(context, data.hasValue());
    SA_TEST_EXPECT(context, data.valueIf()->bytes_read == 3);
    SA_TEST_EXPECT(context, eof.hasValue());
    SA_TEST_EXPECT(context,
                   eof.valueIf()->status == contracts::ReadStatus::end_of_stream);
    auto sticky_eof = pair.second.read(base::ByteSpan{received.data(), received.size()},
                                       base::Deadline::infinite(), noCancellation());
    SA_TEST_EXPECT(context, sticky_eof.hasValue());
    SA_TEST_EXPECT(context, sticky_eof.valueIf()->status == contracts::ReadStatus::end_of_stream);

    // A write half-close does not close the opposite direction.
    const base::ConstByteSpan reply{fixtures::alternating_bytes.data(), 2};
    SA_TEST_EXPECT(context,
                   pair.second.writeSome(reply, base::Deadline::infinite(),
                                         noCancellation()).hasValue());
}

void closingReadRejectsPeerWrites(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    SA_TEST_EXPECT(context, pair.second.shutdown(contracts::ShutdownMode::receive).hasValue());
    const base::ConstByteSpan bytes{fixtures::ascending_bytes.data(), 1};
    auto result = pair.first.writeSome(bytes, base::Deadline::infinite(),
                                       noCancellation());
    SA_TEST_EXPECT(context, !result.hasValue());
    SA_TEST_EXPECT(context, result.errorIf()->code ==
                                base::TransportErrorCode::connection_reset);
}

void byteByByteReadPreservesOrder(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    pair.second.readFaults().setMaxChunk(1);
    const base::ConstByteSpan input{fixtures::ascending_bytes.data(), fixtures::ascending_bytes.size()};
    SA_TEST_EXPECT(context, pair.first.writeAll(input, base::Deadline::infinite(), {}).hasValue());
    std::array<std::uint8_t, fixtures::ascending_bytes.size()> output{};
    for (std::size_t i = 0; i < output.size(); ++i) {
        auto read = pair.second.read({output.data() + i, 1}, base::Deadline::infinite(), {});
        SA_TEST_EXPECT(context, read.hasValue());
        SA_TEST_EXPECT(context, read.valueIf()->bytes_read == 1);
    }
    SA_TEST_EXPECT(context, output == fixtures::ascending_bytes);
}

void writeAllChunksAndCountsPlanCalls(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    pair.first.writeFaults().setMaxChunk(2);
    const base::ConstByteSpan input{fixtures::ascending_bytes.data(), fixtures::ascending_bytes.size()};
    SA_TEST_EXPECT(context, pair.first.writeAll(input, base::Deadline::infinite(), {}).hasValue());
    SA_TEST_EXPECT(context, pair.first.stats().write_calls == 5);
    std::array<std::uint8_t, fixtures::ascending_bytes.size()> output{};
    auto read = pair.second.read({output.data(), output.size()}, base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, read.hasValue());
    SA_TEST_EXPECT(context, read.valueIf()->bytes_read == output.size());
    SA_TEST_EXPECT(context, output == fixtures::ascending_bytes);
}

void writeAllFaultRecoversOnNextChunk(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    pair.first.writeFaults().setMaxChunk(2);
    pair.first.writeFaults().failCall(2, {base::TransportErrorCode::io_failure, 9});
    const base::ConstByteSpan input{fixtures::ascending_bytes.data(), 5};
    auto failed = pair.first.writeAll(input, base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, !failed.hasValue());
    SA_TEST_EXPECT(context, failed.errorIf()->code == base::TransportErrorCode::io_failure);
    auto recovered = pair.first.writeAll({input.data() + 2, 3}, base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, recovered.hasValue());
    SA_TEST_EXPECT(context, pair.second.pendingBytes() == 5);
}

void wakeupsCoalesceAndWouldBlock(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    pair.first.wakeup();
    pair.first.wakeup();
    std::uint8_t byte = 0;
    auto wake = pair.first.read({&byte, 1}, base::Deadline::infinite(), {});
    auto blocked = pair.first.read({&byte, 1}, base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, wake.hasValue() && wake.valueIf()->status == contracts::ReadStatus::woken_up);
    SA_TEST_EXPECT(context, !blocked.hasValue() && blocked.errorIf()->code == base::TransportErrorCode::timed_out);
    SA_TEST_EXPECT(context, pair.first.stats().would_block == 1);
    SA_TEST_EXPECT(context, pair.first.stats().wakeups_observed == 1);
}

void dataPrecedesPendingWakeup(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    const base::ConstByteSpan input{fixtures::alternating_bytes.data(), 1};
    SA_TEST_EXPECT(context, pair.second.writeAll(input, base::Deadline::infinite(), {}).hasValue());
    pair.first.wakeup();
    std::uint8_t byte = 0;
    auto data = pair.first.read({&byte, 1}, base::Deadline::infinite(), {});
    auto wake = pair.first.read({&byte, 1}, base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, data.hasValue() && data.valueIf()->status == contracts::ReadStatus::data);
    SA_TEST_EXPECT(context, byte == fixtures::alternating_bytes[0]);
    SA_TEST_EXPECT(context, wake.hasValue() && wake.valueIf()->status == contracts::ReadStatus::woken_up);
}

void cancellationPrecedesWakeup(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    pair.first.wakeup();
    base::CancellationSource source;
    source.cancel();
    std::uint8_t byte = 0;
    auto cancelled = pair.first.read({&byte, 1}, base::Deadline::infinite(), source.token());
    auto wake = pair.first.read({&byte, 1}, base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, !cancelled.hasValue() && cancelled.errorIf()->code == base::TransportErrorCode::cancelled);
    SA_TEST_EXPECT(context, wake.hasValue() && wake.valueIf()->status == contracts::ReadStatus::woken_up);
}

void expiredDeadlineDoesNotConsumeData(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    const base::ConstByteSpan input{fixtures::alternating_bytes.data(), 1};
    SA_TEST_EXPECT(context, pair.second.writeAll(input, base::Deadline::infinite(), {}).hasValue());
    const auto deadline = in(clock, std::chrono::milliseconds{1});
    clock.advance(std::chrono::milliseconds{1});
    std::uint8_t byte = 0;
    auto expired = pair.first.read({&byte, 1}, deadline, {});
    SA_TEST_EXPECT(context, !expired.hasValue() && expired.errorIf()->code == base::TransportErrorCode::timed_out);
    SA_TEST_EXPECT(context, pair.first.pendingBytes() == 1);
}

void emptyReadSkipsEofAndWakes(test::TestContext& context) {
    test::FakeClock clock;
    auto pair = test::FakeDuplexTransport::create(clock);
    SA_TEST_EXPECT(context, pair.second.shutdown(contracts::ShutdownMode::send).hasValue());
    auto empty = pair.first.read({}, base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, empty.hasValue());
    SA_TEST_EXPECT(context, empty.valueIf()->bytes_read == 0);
    SA_TEST_EXPECT(context, empty.valueIf()->status == contracts::ReadStatus::data);
    std::uint8_t byte = 0;
    auto eof = pair.first.read({&byte, 1}, base::Deadline::infinite(), {});
    SA_TEST_EXPECT(context, eof.hasValue());
    SA_TEST_EXPECT(context, eof.valueIf()->status == contracts::ReadStatus::end_of_stream);
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
        {"byte-by-byte read preserves order", byteByByteReadPreservesOrder},
        {"writeAll chunks and counts plan calls", writeAllChunksAndCountsPlanCalls},
        {"writeAll fault recovers on next chunk", writeAllFaultRecoversOnNextChunk},
        {"wakeups coalesce and would-block", wakeupsCoalesceAndWouldBlock},
        {"data precedes pending wakeup", dataPrecedesPendingWakeup},
        {"cancellation precedes wakeup", cancellationPrecedesWakeup},
        {"expired deadline does not consume data", expiredDeadlineDoesNotConsumeData},
        {"empty read skips EOF and wakeup", emptyReadSkipsEofAndWakes},
    });
}
