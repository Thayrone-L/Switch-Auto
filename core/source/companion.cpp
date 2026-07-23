#include "switch_auto/companion.hpp"
#include "switch_auto/crypto.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace switch_auto {
namespace {

constexpr std::size_t kMaximumMapFrameSize = 512 * 1024;

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

std::optional<double> numberField(std::string_view json, std::string_view name) {
    const std::string key = "\"" + std::string(name) + "\"";
    std::size_t position = json.find(key);
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find(':', position + key.size());
    if (position == std::string_view::npos) return std::nullopt;
    ++position;
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    const std::string remaining(json.substr(position));
    char* end = nullptr;
    const double value = std::strtod(remaining.c_str(), &end);
    if (end == remaining.c_str()) return std::nullopt;
    return value;
}

std::optional<std::string> objectField(
    std::string_view json, std::string_view name) {
    const std::string key = "\"" + std::string(name) + "\"";
    std::size_t position = json.find(key);
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find(':', position + key.size());
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find('{', position + 1);
    if (position == std::string_view::npos) return std::nullopt;
    std::size_t depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t index = position; index < json.size(); ++index) {
        const char value = json[index];
        if (inString) {
            if (escaped) escaped = false;
            else if (value == '\\') escaped = true;
            else if (value == '"') inString = false;
            continue;
        }
        if (value == '"') inString = true;
        else if (value == '{') ++depth;
        else if (value == '}' && --depth == 0) {
            return std::string(json.substr(position, index - position + 1));
        }
    }
    return std::nullopt;
}

bool validPairingCode(std::string_view code) {
    return code.size() == 64 &&
        std::all_of(code.begin(), code.end(), [](char value) {
            return std::isxdigit(static_cast<unsigned char>(value));
        });
}

std::string envelope(std::string_view payload) {
    return "{\"version\":" + std::to_string(kProtocolVersion) + "," +
        std::string(payload) + "}\n";
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
        if (line.find("\"location\"") != std::string_view::npos) {
            const auto latitude = numberField(line, "latitude");
            const auto longitude = numberField(line, "longitude");
            const auto accuracy = numberField(line, "accuracyMeters");
            if (latitude && longitude && accuracy &&
                *latitude >= -90.0 && *latitude <= 90.0 &&
                *longitude >= -180.0 && *longitude <= 180.0 &&
                *accuracy >= 0.0) {
                RemoteLocation location{
                    *latitude, *longitude, *accuracy, std::nullopt, std::nullopt};
                const auto speed = numberField(line, "speedMetersPerSecond");
                if (speed && *speed >= 0.0) location.speedMetersPerSecond = *speed;
                const auto bearing = numberField(line, "bearingDegrees");
                if (bearing && *bearing >= 0.0 && *bearing <= 360.0) {
                    location.bearingDegrees = *bearing;
                }
                status_.remoteLocation = location;
            } else {
                status_.remoteLocation.reset();
            }
        } else {
            status_.remoteLocation.reset();
        }
        pendingMapFrame_.reset();
        pendingBinaryLength_.reset();
        if (const auto mapFrame = objectField(line, "mapFrame")) {
            const auto id = unsignedField(*mapFrame, "id");
            const auto length = unsignedField(*mapFrame, "length");
            const auto width = unsignedField(*mapFrame, "width");
            const auto height = unsignedField(*mapFrame, "height");
            const auto contentType = stringField(*mapFrame, "contentType");
            if (!length || *length > kMaximumMapFrameSize) {
                return fail(CompanionError::InvalidMessage);
            }
            if (*length > 0) {
                pendingMapFrame_ = RemoteMapFrame{
                    id.value_or(0),
                    width && *width > 0 && *width <= 1280 ? *width : 640,
                    height && *height > 0 && *height <= 720 ? *height : 360,
                    contentType && *contentType == "image/jpeg"
                        ? *contentType : "image/jpeg",
                    {}};
                pendingBinaryLength_ = *length;
            } else {
                status_.remoteMapFrame.reset();
            }
        } else {
            status_.remoteMapFrame.reset();
        }
        status_.remoteCustomization.reset();
        if (const auto customization = objectField(line, "customization")) {
            const auto schema = unsignedField(*customization, "schemaVersion");
            const auto revision = unsignedField(*customization, "revision");
            const auto themeId = stringField(*customization, "themeId");
            const auto layoutId = stringField(*customization, "layoutId");
            const auto layout = objectField(*customization, "layout");
            if (schema && *schema == 1 && revision && *revision > 0 &&
                themeId && !themeId->empty() && layoutId && !layoutId->empty() &&
                layout) {
                status_.remoteCustomization = RemoteCustomization{
                    *revision, *themeId, *layoutId, *layout};
            }
        }
        pendingSequence_.reset();
        return Result<std::optional<std::string>, CompanionError>::success(std::nullopt);
    }

    return fail(CompanionError::UnexpectedMessage);
}

std::optional<std::size_t> CompanionSession::pendingBinaryLength() const {
    return pendingBinaryLength_;
}

Result<void, CompanionError> CompanionSession::acceptBinaryPayload(
    std::vector<std::uint8_t> payload) {
    if (!pendingMapFrame_ || !pendingBinaryLength_ ||
        payload.size() != *pendingBinaryLength_) {
        pendingMapFrame_.reset();
        pendingBinaryLength_.reset();
        return Result<void, CompanionError>::failure(CompanionError::InvalidMessage);
    }
    pendingMapFrame_->data = std::move(payload);
    status_.remoteMapFrame = std::move(pendingMapFrame_);
    pendingMapFrame_.reset();
    pendingBinaryLength_.reset();
    return Result<void, CompanionError>::success();
}

Result<std::string, CompanionError> CompanionSession::heartbeat(
    std::optional<int> localBatteryPercent,
    std::optional<std::uint32_t> customizationRevision,
    std::optional<float> mapPanXPixels,
    std::optional<float> mapPanYPixels) {
    if (status_.state != CompanionConnectionState::Online || pendingSequence_) {
        return Result<std::string, CompanionError>::failure(CompanionError::NotOnline);
    }
    pendingSequence_ = nextSequence_++;
    const std::string batteryField = localBatteryPercent && *localBatteryPercent >= 0 &&
            *localBatteryPercent <= 100
        ? ",\"switchBatteryPercent\":" + std::to_string(*localBatteryPercent)
        : "";
    const std::string customizationField =
        customizationRevision && *customizationRevision > 0
        ? ",\"customizationRevision\":" + std::to_string(*customizationRevision)
        : "";
    const std::string mapViewportField =
        mapPanXPixels && mapPanYPixels &&
            (*mapPanXPixels != 0.0f || *mapPanYPixels != 0.0f)
        ? ",\"mapViewport\":{\"deltaX\":" + std::to_string(*mapPanXPixels) +
            ",\"deltaY\":" + std::to_string(*mapPanYPixels) + "}"
        : "";
    return Result<std::string, CompanionError>::success(envelope(
        "\"type\":\"ping\",\"sequence\":" + std::to_string(*pendingSequence_) +
        batteryField + customizationField + mapViewportField));
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
    pendingMapFrame_.reset();
    pendingBinaryLength_.reset();
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
