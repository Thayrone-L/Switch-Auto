#include "core/base/byte_span.hpp"
#include "core/base/cancellation.hpp"
#include "core/base/errors.hpp"
#include "core/base/result.hpp"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace base = switch_auto::base;

namespace {

int failures = 0;

template <typename T, typename = void>
struct CanIssueTokenFromRvalue : std::false_type {};

template <typename T>
struct CanIssueTokenFromRvalue<
    T, std::void_t<decltype(std::declval<T&&>().token())>> : std::true_type {};

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,     \
                         __LINE__, #condition);                                 \
            ++failures;                                                        \
        }                                                                       \
    } while (false)

void byteSpanPreservesBounds() {
    std::uint8_t bytes[]{1, 2, 3, 4};
    base::ByteSpan span{bytes};
    const base::ConstByteSpan read_only = span;
    CHECK(span.size() == 4);
    CHECK(read_only.data() == bytes);
    CHECK(span.subspan(1, 2).size() == 2);
    CHECK(span.subspan(1, 2)[0] == 2);
    CHECK(span.subspan(3, 99).size() == 1);
    CHECK(span.subspan(5).empty());
}

void resultDistinguishesValueAndError() {
    auto success = base::Result<std::uint32_t, base::TransportError>::success(42);
    CHECK(success.hasValue());
    CHECK(success.valueIf() != nullptr);
    CHECK(*success.valueIf() == 42);
    CHECK(success.errorIf() == nullptr);

    const base::TransportError expected{
        base::TransportErrorCode::connection_reset, 10054};
    auto failure =
        base::Result<std::uint32_t, base::TransportError>::failure(expected);
    CHECK(!failure.hasValue());
    CHECK(failure.valueIf() == nullptr);
    CHECK(failure.errorIf() != nullptr);
    CHECK(failure.errorIf()->code == base::TransportErrorCode::connection_reset);
    CHECK(failure.errorIf()->platform_code == 10054);

    auto void_success = base::Result<void, base::ProtocolError>::success();
    CHECK(void_success.hasValue());
    CHECK(void_success.errorIf() == nullptr);

    auto void_failure = base::Result<void, base::ProtocolError>::failure(
        {base::ProtocolErrorCode::invalid_header, 1, 2});
    CHECK(!void_failure.hasValue());
    CHECK(void_failure.errorIf() != nullptr);
    CHECK(void_failure.errorIf()->code ==
          base::ProtocolErrorCode::invalid_header);
}

base::CancellationToken retainedCancelledToken() {
    base::CancellationSource source;
    auto token = source.token();
    source.cancel();
    return token;
}

void cancellationIsExplicitAndLifetimeSafe() {
    base::CancellationToken inert;
    CHECK(!inert.canBeCancelled());
    CHECK(!inert.isCancellationRequested());

    base::CancellationSource source;
    const auto token = source.token();
    CHECK(token.canBeCancelled());
    CHECK(!token.isCancellationRequested());
    source.cancel();
    CHECK(token.isCancellationRequested());

    const auto retained = retainedCancelledToken();
    CHECK(retained.canBeCancelled());
    CHECK(retained.isCancellationRequested());
}

void deadlinesUseMonotonicTime() {
    const auto now = base::Deadline::Clock::now();
    CHECK(base::Deadline::infinite().isInfinite());
    CHECK(!base::Deadline::infinite().hasExpired(now));
    CHECK(base::Deadline::at(now).hasExpired(now));
    CHECK(!base::Deadline::at(now + std::chrono::seconds{1}).hasExpired(now));
}

void errorsCarryTypedContext() {
    const base::ProtocolError protocol{
        base::ProtocolErrorCode::unexpected_message, 3, 7};
    const auto session = base::SessionError::fromProtocol(protocol);
    CHECK(session.code == base::SessionErrorCode::protocol_failure);
    CHECK(session.protocol.channel_id == 3);
    CHECK(session.protocol.message_id == 7);
}

static_assert(std::is_nothrow_copy_constructible_v<base::ByteSpan>);
static_assert(std::is_trivially_copyable_v<base::TransportError>);
static_assert(std::is_trivially_copyable_v<base::ProtocolError>);
static_assert(std::is_trivially_copyable_v<base::SessionError>);
static_assert(!CanIssueTokenFromRvalue<base::CancellationSource>::value);

}  // namespace

int main() {
    byteSpanPreservesBounds();
    resultDistinguishesValueAndError();
    cancellationIsExplicitAndLifetimeSafe();
    deadlinesUseMonotonicTime();
    errorsCarryTypedContext();
    return failures == 0 ? 0 : 1;
}
