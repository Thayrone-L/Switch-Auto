#pragma once

#include <cstdint>

namespace switch_auto::base {

enum class TransportErrorCode : std::uint8_t {
    unavailable,
    connection_refused,
    connection_reset,
    end_of_stream,
    timed_out,
    cancelled,
    invalid_argument,
    io_failure,
    unsupported,
};

struct TransportError {
    TransportErrorCode code = TransportErrorCode::io_failure;
    std::int32_t platform_code = 0;
};

enum class ProtocolErrorCode : std::uint8_t {
    malformed_frame,
    invalid_header,
    invalid_size,
    unsupported_version,
    unexpected_message,
    sequence_violation,
    crypto_failure,
    resource_limit,
};

struct ProtocolError {
    ProtocolErrorCode code = ProtocolErrorCode::malformed_frame;
    std::uint32_t channel_id = 0;
    std::uint32_t message_id = 0;
};

enum class SessionErrorCode : std::uint8_t {
    transport_failure,
    protocol_failure,
    state_violation,
    negotiation_failed,
    service_unavailable,
    timed_out,
    cancelled,
    peer_closed,
};

struct SessionError {
    SessionErrorCode code = SessionErrorCode::state_violation;
    TransportError transport{};
    ProtocolError protocol{};

    [[nodiscard]] static constexpr SessionError fromTransport(
        TransportError error) noexcept {
        SessionError result{};
        result.code = SessionErrorCode::transport_failure;
        result.transport = error;
        return result;
    }

    [[nodiscard]] static constexpr SessionError fromProtocol(
        ProtocolError error) noexcept {
        SessionError result{};
        result.code = SessionErrorCode::protocol_failure;
        result.protocol = error;
        return result;
    }
};

}  // namespace switch_auto::base
