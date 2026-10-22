#pragma once

#include "core/base/byte_span.hpp"
#include "core/base/cancellation.hpp"
#include "core/base/errors.hpp"
#include "core/base/result.hpp"

#include <cstddef>
#include <cstdint>

namespace switch_auto::contracts {

enum class ReadStatus : std::uint8_t {
    data,
    end_of_stream,
    woken_up,
};

struct ReadResult {
    std::size_t bytes_read = 0;
    ReadStatus status = ReadStatus::data;
};

enum class ShutdownMode : std::uint8_t {
    receive,
    send,
    both,
};

// A byte-stream transport with no knowledge of TLS or application framing.
//
// Ownership and concurrency:
// - The caller owns all spans for the duration of each call.
// - At most one read and one writeAll may be in flight. Implementations may
//   support those two calls concurrently, but calls in the same direction must
//   never overlap.
// - wakeup() and shutdown() may be called from another thread. Destruction is
//   only legal after all in-flight calls have returned and their threads joined.
//
// Blocking and interruption:
// - Deadlines cover the complete operation and use the monotonic Deadline clock.
// - Cancellation is cooperative: after cancelling a retained token, the owner
//   calls wakeup() to interrupt a platform wait immediately. A cancelled call
//   returns TransportErrorCode::cancelled, never woken_up.
// - A wakeup without cancellation makes a blocked read return woken_up with
//   zero bytes. It interrupts a blocked writeAll with unavailable; callers may
//   retry the complete message according to their higher-level ownership rules.
//   Wakeups are level-triggered and coalesced: at least one remains pending
//   until observed by an operation, while repeated wakeups may merge.
// - Implementations must not use unbounded polling to observe cancellation.
// - Deadline expiry returns timed_out. A deadline already expired must not start
//   platform I/O. Empty read/write spans complete immediately.
//
// Stream semantics:
// - read may return any positive number up to target.size(); short reads are
//   normal. For a non-empty target, data always has bytes_read > 0.
// - end_of_stream is a successful result with zero bytes and is sticky after
//   peer send-half closure. Empty targets return data with zero bytes and do not
//   probe EOF. Adapters do not report TransportErrorCode::end_of_stream for this
//   interface.
// - writeAll success means every byte was accepted in order. Internal short
//   writes are hidden. On failure no resumable byte count is exposed; protocol
//   code must treat the stream/session as indeterminate rather than replaying a
//   suffix blindly.
// - shutdown(send) produces a local send-half close while receive stays usable;
//   shutdown(receive) wakes reads; both is their union. It is idempotent.
//   shutdown is non-blocking and does not perform a TLS close notification.
class ITransport {
public:
    virtual ~ITransport() = default;

    [[nodiscard]] virtual base::Result<ReadResult, base::TransportError> read(
        base::ByteSpan target,
        base::Deadline deadline,
        base::CancellationToken cancellation) noexcept = 0;

    [[nodiscard]] virtual base::Result<void, base::TransportError> writeAll(
        base::ConstByteSpan bytes,
        base::Deadline deadline,
        base::CancellationToken cancellation) noexcept = 0;

    virtual void wakeup() noexcept = 0;

    [[nodiscard]] virtual base::Result<void, base::TransportError> shutdown(
        ShutdownMode mode) noexcept = 0;
};

}  // namespace switch_auto::contracts
