#pragma once

#include "core/contracts/transport.hpp"

#include <cstdint>

namespace switch_auto::contracts {

enum class TlsErrorCode : std::uint8_t {
    transport_failure,
    handshake_failed,
    peer_closed,
    invalid_state,
    timed_out,
    cancelled,
    crypto_failure,
};

struct TlsError {
    TlsErrorCode code = TlsErrorCode::invalid_state;
    std::int32_t implementation_code = 0;
    base::TransportError transport{};
};

enum class TlsReadStatus : std::uint8_t {
    data,
    end_of_stream,
    woken_up,
};

struct TlsReadResult {
    std::size_t bytes_read = 0;
    TlsReadStatus status = TlsReadStatus::data;
};

// TLS is driven only by the Protocol I/O owner thread; no calls overlap. The
// engine borrows the transport and never retains spans or CancellationTokens
// after a call returns.
// handshake() must complete before read/writeAll. Short plaintext reads are
// normal; writeAll success covers the complete plaintext span. Deadlines and
// cancellation cover all nested transport work. The caller uses the borrowed
// transport's wakeup() after cancelling. Transport wakeup maps to woken_up for
// reads unless cancellation was requested. A clean TLS close is end_of_stream;
// truncation or invalid close is peer_closed. shutdown() sends the TLS close
// notification when possible but never owns or destroys the transport.
class ITlsEngine {
public:
    virtual ~ITlsEngine() = default;

    [[nodiscard]] virtual base::Result<void, TlsError> handshake(
        ITransport& transport,
        base::Deadline deadline,
        base::CancellationToken cancellation) noexcept = 0;

    [[nodiscard]] virtual base::Result<TlsReadResult, TlsError> read(
        ITransport& transport,
        base::ByteSpan plaintext,
        base::Deadline deadline,
        base::CancellationToken cancellation) noexcept = 0;

    [[nodiscard]] virtual base::Result<void, TlsError> writeAll(
        ITransport& transport,
        base::ConstByteSpan plaintext,
        base::Deadline deadline,
        base::CancellationToken cancellation) noexcept = 0;

    [[nodiscard]] virtual base::Result<void, TlsError> shutdown(
        ITransport& transport,
        base::Deadline deadline,
        base::CancellationToken cancellation) noexcept = 0;
};

}  // namespace switch_auto::contracts
