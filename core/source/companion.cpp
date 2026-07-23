#include "switch_auto/companion.hpp"
#include "switch_auto/crypto.hpp"

#include <algorithm>
#include <cctype>

namespace switch_auto {
namespace {

std::optional<std::string> stringField(std::string_view json, std::string_view name) {
    const std::string key = "\"" + std::string(name) + "\"";
    std::size_t position = json.find(key);
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find(':', position + key.size());
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find('"', position + 1);
    if (position == std::string_view::npos) return std::nullopt;
    const std::size_t end = json.find('"', position + 1);
    if (end == std::string_view::npos) return std::nullopt;
    return std::string(json.substr(position + 1, end - position - 1));
}

std::optional<std::uint32_t> unsignedField(std::string_view json, std::string_view name) {
    const std::string key = "\"" + std::string(name) + "\"";
    std::size_t position = json.find(key);
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find(':', position + key.size());
    if (position == std::string_view::npos) return std::nullopt;
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    if (position == json.size() || !std::isdigit(static_cast<unsigned char>(json[position]))) return std::nullopt;
    std::uint32_t value = 0;
    while (position < json.size() && std::isdigit(static_cast<unsigned char>(json[position]))) {
        value = value * 10 + static_cast<std::uint32_t>(json[position++] - '0');
    }
    return value;
}

bool validPairingCode(std::string_view code) {
    return code.size() == 64 &&
        std::all_of(code.begin(), code.end(), [](char value) {
            return std::isxdigit(static_cast<unsigned char>(value));
        });
}

std::string envelope(std::string_view payload) {
    return "{\"version\":2," + std::string(payload) + "}\n";
}

}  // namespace

Result<std::string, CompanionError> CompanionSession::begin(
    std::string_view pairingToken, std::string_view deviceId,
    std::string_view clientNonce, std::string_view sessionId) {
    reset();
    if (!validPairingCode(pairingToken) || deviceId.empty() || clientNonce.size() != 32) {
        status_.state = CompanionConnectionState::Failed;
        status_.lastError = CompanionError::InvalidPairingCode;
        return Result<std::string, CompanionError>::failure(status_.lastError);
    }
    status_.state = CompanionConnectionState::Pairing;
    pairingToken_ = pairingToken;
    deviceId_ = deviceId;
    clientNonce_ = clientNonce;
    const std::string sessionField = sessionId.empty()
        ? "" : ",\"sessionId\":\"" + std::string(sessionId) + "\"";
    return Result<std::string, CompanionError>::success(envelope(
        "\"type\":\"pair.request\",\"deviceName\":\"Nintendo Switch\",\"deviceId\":\"" +
        deviceId_ + "\",\"clientNonce\":\"" + clientNonce_ + "\"" + sessionField));
}

void CompanionSession::transportConnected() {
    status_.state = CompanionConnectionState::Connecting;
    status_.lastError = CompanionError::None;
}

Result<std::optional<std::string>, CompanionError> CompanionSession::receive(std::string_view line) {
    const auto version = unsignedField(line, "version");
    const auto type = stringField(line, "type");
    if (!version || !type) return fail(CompanionError::InvalidMessage);
    if (*version != static_cast<std::uint32_t>(kProtocolVersion)) {
        return fail(CompanionError::UnsupportedVersion);
    }

    if (status_.state == CompanionConnectionState::Pairing) {
        if (*type == "pair.rejected") return fail(CompanionError::PairingRejected);
        if (*type != "pair.challenge") return fail(CompanionError::UnexpectedMessage);
        const auto serverNonce = stringField(line, "serverNonce");
        const auto proof = stringField(line, "proof");
        if (!serverNonce || serverNonce->size() != 32 || !proof) {
            return fail(CompanionError::InvalidMessage);
        }
        const std::string transcript = "server|" + deviceId_ + "|" + clientNonce_ +
            "|" + *serverNonce;
        const std::string expected = crypto::hmacSha256Hex(pairingToken_, transcript);
        if (!crypto::constantTimeEquals(expected, *proof)) {
            return fail(CompanionError::AuthenticationFailed);
        }
        serverNonce_ = *serverNonce;
        status_.state = CompanionConnectionState::Authenticating;
        const std::string clientProof = crypto::hmacSha256Hex(pairingToken_,
            "client|" + deviceId_ + "|" + clientNonce_ + "|" + serverNonce_);
        return Result<std::optional<std::string>, CompanionError>::success(envelope(
            "\"type\":\"pair.proof\",\"deviceId\":\"" + deviceId_ +
            "\",\"proof\":\"" + clientProof + "\""));
    }

    if (status_.state == CompanionConnectionState::Authenticating) {
        if (*type == "pair.rejected") return fail(CompanionError::PairingRejected);
        if (*type != "pair.accepted") return fail(CompanionError::UnexpectedMessage);
        status_.sessionToken = stringField(line, "sessionToken");
        if (!status_.sessionToken || status_.sessionToken->empty()) {
            return fail(CompanionError::InvalidMessage);
        }
        status_.state = CompanionConnectionState::Handshaking;
        return Result<std::optional<std::string>, CompanionError>::success(
            envelope("\"type\":\"hello\",\"client\":\"switch-auto\""));
    }

    if (status_.state == CompanionConnectionState::Handshaking) {
        if (*type != "hello.ack") return fail(CompanionError::UnexpectedMessage);
        status_.state = CompanionConnectionState::Online;
        status_.lastError = CompanionError::None;
        return Result<std::optional<std::string>, CompanionError>::success(std::nullopt);
    }

    if (status_.state == CompanionConnectionState::Online) {
        if (*type != "pong") return fail(CompanionError::UnexpectedMessage);
        const auto sequence = unsignedField(line, "sequence");
        if (!sequence || !pendingSequence_ || *sequence != *pendingSequence_) {
            return fail(CompanionError::HeartbeatMismatch);
        }
        status_.lastSequence = *sequence;
        const auto remoteBattery = unsignedField(line, "phoneBatteryPercent");
        if (remoteBattery && *remoteBattery <= 100) {
            status_.remoteBatteryPercent = static_cast<int>(*remoteBattery);
        }
        pendingSequence_.reset();
        return Result<std::optional<std::string>, CompanionError>::success(std::nullopt);
    }

    return fail(CompanionError::UnexpectedMessage);
}

Result<std::string, CompanionError> CompanionSession::heartbeat(
    std::optional<int> localBatteryPercent) {
    if (status_.state != CompanionConnectionState::Online || pendingSequence_) {
        return Result<std::string, CompanionError>::failure(CompanionError::NotOnline);
    }
    pendingSequence_ = nextSequence_++;
    const std::string batteryField = localBatteryPercent && *localBatteryPercent >= 0 &&
            *localBatteryPercent <= 100
        ? ",\"switchBatteryPercent\":" + std::to_string(*localBatteryPercent)
        : "";
    return Result<std::string, CompanionError>::success(envelope(
        "\"type\":\"ping\",\"sequence\":" + std::to_string(*pendingSequence_) +
        batteryField));
}

void CompanionSession::transportDisconnected(CompanionError reason) {
    pendingSequence_.reset();
    status_.state = CompanionConnectionState::Reconnecting;
    status_.lastError = reason;
}

void CompanionSession::reset() {
    status_ = {};
    nextSequence_ = 1;
    pendingSequence_.reset();
    pairingToken_.clear();
    deviceId_.clear();
    clientNonce_.clear();
    serverNonce_.clear();
}

Result<std::optional<std::string>, CompanionError> CompanionSession::fail(CompanionError error) {
    status_.state = CompanionConnectionState::Failed;
    status_.lastError = error;
    pendingSequence_.reset();
    return Result<std::optional<std::string>, CompanionError>::failure(error);
}

const char* toString(CompanionConnectionState state) {
    switch (state) {
        case CompanionConnectionState::Disconnected: return "disconnected";
        case CompanionConnectionState::Discovering: return "discovering";
        case CompanionConnectionState::Connecting: return "connecting";
        case CompanionConnectionState::Pairing: return "pairing";
        case CompanionConnectionState::Authenticating: return "authenticating";
        case CompanionConnectionState::Handshaking: return "handshaking";
        case CompanionConnectionState::Online: return "online";
        case CompanionConnectionState::Reconnecting: return "reconnecting";
        case CompanionConnectionState::Failed: return "failed";
    }
    return "unknown";
}

const char* toString(CompanionError error) {
    switch (error) {
        case CompanionError::None: return "none";
        case CompanionError::InvalidEndpoint: return "invalid_endpoint";
        case CompanionError::InvalidPairingCode: return "invalid_pairing_code";
        case CompanionError::InvalidMessage: return "invalid_message";
        case CompanionError::UnsupportedVersion: return "unsupported_version";
        case CompanionError::UnexpectedMessage: return "unexpected_message";
        case CompanionError::PairingRejected: return "pairing_rejected";
        case CompanionError::AuthenticationFailed: return "authentication_failed";
        case CompanionError::NotOnline: return "not_online";
        case CompanionError::HeartbeatMismatch: return "heartbeat_mismatch";
        case CompanionError::TransportUnavailable: return "transport_unavailable";
        case CompanionError::DiscoveryTimeout: return "discovery_timeout";
        case CompanionError::ConnectionTimeout: return "connection_timeout";
        case CompanionError::ConnectionClosed: return "connection_closed";
    }
    return "unknown";
}

}  // namespace switch_auto
