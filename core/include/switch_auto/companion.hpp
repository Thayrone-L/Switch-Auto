#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "switch_auto/result.hpp"

namespace switch_auto {

inline constexpr std::uint16_t kCompanionPort = 38471;
inline constexpr int kProtocolVersion = 2;

enum class CompanionConnectionState {
    Disconnected,
    Discovering,
    Connecting,
    Pairing,
    Authenticating,
    Handshaking,
    Online,
    Reconnecting,
    Failed,
};

enum class CompanionError {
    None,
    InvalidEndpoint,
    InvalidPairingCode,
    InvalidMessage,
    UnsupportedVersion,
    UnexpectedMessage,
    PairingRejected,
    AuthenticationFailed,
    NotOnline,
    HeartbeatMismatch,
    TransportUnavailable,
    DiscoveryTimeout,
    ConnectionTimeout,
    ConnectionClosed,
};

struct CompanionEndpoint {
    std::string address;
    std::uint16_t port = kCompanionPort;
};

struct CompanionStatus {
    CompanionConnectionState state = CompanionConnectionState::Disconnected;
    CompanionError lastError = CompanionError::None;
    std::optional<std::string> sessionToken;
    std::uint32_t lastSequence = 0;
    std::optional<int> remoteBatteryPercent;
    std::string endpointAddress;
    std::uint32_t retryAttempt = 0;
    std::uint32_t retryDelaySeconds = 0;
};

struct CompanionPairingInfo {
    std::string deviceId;
    std::string code;
    std::string pairingToken;
    std::string qrPayload;
};

class CompanionService {
public:
    virtual ~CompanionService() = default;
    virtual Result<void, CompanionError> connect(
        const CompanionEndpoint& endpoint, std::string_view pairingCode) = 0;
    virtual Result<void, CompanionError> startAutomaticPairing() = 0;
    virtual void update() = 0;
    virtual void disconnect() = 0;
    virtual CompanionStatus status() const = 0;
    virtual std::optional<CompanionPairingInfo> pairingInfo() const = 0;
    virtual bool hasTrustedCompanion() const = 0;
};

// Transport-independent protocol state machine. Platform services own sockets,
// threads and timeouts; this class only validates and advances the session.
class CompanionSession {
public:
    Result<std::string, CompanionError> begin(
        std::string_view pairingToken, std::string_view deviceId,
        std::string_view clientNonce);
    Result<std::optional<std::string>, CompanionError> receive(std::string_view line);
    Result<std::string, CompanionError> heartbeat(std::optional<int> localBatteryPercent = std::nullopt);
    void transportConnected();
    void transportDisconnected(CompanionError reason);
    void reset();

    const CompanionStatus& status() const { return status_; }

private:
    Result<std::optional<std::string>, CompanionError> fail(CompanionError error);

    CompanionStatus status_;
    std::uint32_t nextSequence_ = 1;
    std::optional<std::uint32_t> pendingSequence_;
    std::string pairingToken_;
    std::string deviceId_;
    std::string clientNonce_;
    std::string serverNonce_;
};

const char* toString(CompanionConnectionState state);
const char* toString(CompanionError error);

}  // namespace switch_auto
