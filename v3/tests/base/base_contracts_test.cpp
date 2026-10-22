#include "core/base/byte_span.hpp"
#include "core/base/cancellation.hpp"
#include "core/base/errors.hpp"
#include "core/base/result.hpp"
#include "tests/support/test_runner.hpp"

#include <chrono>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace base = switch_auto::base;
namespace test = switch_auto::test;

namespace {

template <typename T, typename = void>
struct CanIssueTokenFromRvalue : std::false_type {};

template <typename T>
struct CanIssueTokenFromRvalue<
    T, std::void_t<decltype(std::declval<T&&>().token())>> : std::true_type {};

void byteSpanPreservesBounds(test::TestContext& context) {
    std::uint8_t bytes[]{1, 2, 3, 4};
    base::ByteSpan span{bytes};
    const base::ConstByteSpan read_only = span;
    SA_TEST_EXPECT(context, span.size() == 4);
    SA_TEST_EXPECT(context, read_only.data() == bytes);
    SA_TEST_EXPECT(context, span.subspan(1, 2).size() == 2);
    SA_TEST_EXPECT(context, span.subspan(1, 2)[0] == 2);
    SA_TEST_EXPECT(context, span.subspan(3, 99).size() == 1);
    SA_TEST_EXPECT(context, span.subspan(5).empty());
}

void resultDistinguishesValueAndError(test::TestContext& context) {
    auto success = base::Result<std::uint32_t, base::TransportError>::success(42);
    SA_TEST_EXPECT(context, success.hasValue());
    SA_TEST_EXPECT(context, success.valueIf() != nullptr);
    SA_TEST_EXPECT(context, *success.valueIf() == 42);
    SA_TEST_EXPECT(context, success.errorIf() == nullptr);
    const base::TransportError expected{base::TransportErrorCode::connection_reset, 10054};
    auto failure = base::Result<std::uint32_t, base::TransportError>::failure(expected);
    SA_TEST_EXPECT(context, !failure.hasValue());
    SA_TEST_EXPECT(context, failure.valueIf() == nullptr);
    SA_TEST_EXPECT(context, failure.errorIf() != nullptr);
    SA_TEST_EXPECT(context, failure.errorIf()->code == base::TransportErrorCode::connection_reset);
    SA_TEST_EXPECT(context, failure.errorIf()->platform_code == 10054);
    auto void_success = base::Result<void, base::ProtocolError>::success();
    SA_TEST_EXPECT(context, void_success.hasValue());
    SA_TEST_EXPECT(context, void_success.errorIf() == nullptr);
    auto void_failure = base::Result<void, base::ProtocolError>::failure(
        {base::ProtocolErrorCode::invalid_header, 1, 2});
    SA_TEST_EXPECT(context, !void_failure.hasValue());
    SA_TEST_EXPECT(context, void_failure.errorIf() != nullptr);
    SA_TEST_EXPECT(context, void_failure.errorIf()->code == base::ProtocolErrorCode::invalid_header);
}

base::CancellationToken retainedCancelledToken() {
    base::CancellationSource source;
    auto token = source.token();
    source.cancel();
    return token;
}

void cancellationIsExplicitAndLifetimeSafe(test::TestContext& context) {
    base::CancellationToken inert;
    SA_TEST_EXPECT(context, !inert.canBeCancelled());
    SA_TEST_EXPECT(context, !inert.isCancellationRequested());
    base::CancellationSource source;
    const auto token = source.token();
    SA_TEST_EXPECT(context, token.canBeCancelled());
    SA_TEST_EXPECT(context, !token.isCancellationRequested());
    source.cancel();
    SA_TEST_EXPECT(context, token.isCancellationRequested());
    const auto retained = retainedCancelledToken();
    SA_TEST_EXPECT(context, retained.canBeCancelled());
    SA_TEST_EXPECT(context, retained.isCancellationRequested());
}

void deadlinesUseMonotonicTime(test::TestContext& context) {
    const auto now = base::Deadline::Clock::now();
    SA_TEST_EXPECT(context, base::Deadline::infinite().isInfinite());
    SA_TEST_EXPECT(context, !base::Deadline::infinite().hasExpired(now));
    SA_TEST_EXPECT(context, base::Deadline::at(now).hasExpired(now));
    SA_TEST_EXPECT(context, !base::Deadline::at(now + std::chrono::seconds{1}).hasExpired(now));
}

void errorsCarryTypedContext(test::TestContext& context) {
    const base::ProtocolError protocol{base::ProtocolErrorCode::unexpected_message, 3, 7};
    const auto session = base::SessionError::fromProtocol(protocol);
    SA_TEST_EXPECT(context, session.code == base::SessionErrorCode::protocol_failure);
    SA_TEST_EXPECT(context, session.protocol.channel_id == 3);
    SA_TEST_EXPECT(context, session.protocol.message_id == 7);
}

static_assert(std::is_nothrow_copy_constructible_v<base::ByteSpan>);
static_assert(std::is_trivially_copyable_v<base::TransportError>);
static_assert(std::is_trivially_copyable_v<base::ProtocolError>);
static_assert(std::is_trivially_copyable_v<base::SessionError>);
static_assert(!CanIssueTokenFromRvalue<base::CancellationSource>::value);

}  // namespace

int main() {
    return test::runTests({
        {"byte span preserves bounds", byteSpanPreservesBounds},
        {"result distinguishes value and error", resultDistinguishesValueAndError},
        {"cancellation is explicit and lifetime safe", cancellationIsExplicitAndLifetimeSafe},
        {"deadlines use monotonic time", deadlinesUseMonotonicTime},
        {"errors carry typed context", errorsCarryTypedContext},
    });
}
