#include "switch_auto/android_auto.hpp"

#include <cmath>
#include <utility>

namespace switch_auto {
namespace {

constexpr std::size_t kMaximumVideoPacketBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaximumAudioPacketBytes = 256 * 1024;

bool isStreaming(AndroidAutoHostState state) {
    return state == AndroidAutoHostState::Streaming ||
        state == AndroidAutoHostState::Suspended;
}

}  // namespace

Result<void, AndroidAutoError> AndroidAutoSession::startDiscovery() {
    if (status_.state != AndroidAutoHostState::Stopped &&
        status_.state != AndroidAutoHostState::Failed) {
        return Result<void, AndroidAutoError>::failure(
            AndroidAutoError::InvalidPacket);
    }
    status_ = {};
    status_.state = AndroidAutoHostState::Discovering;
    return Result<void, AndroidAutoError>::success();
}

Result<void, AndroidAutoError> AndroidAutoSession::beginConnection(
    std::string_view endpoint) {
    if (status_.state != AndroidAutoHostState::Discovering || endpoint.empty()) {
        return Result<void, AndroidAutoError>::failure(
            AndroidAutoError::InvalidPacket);
    }
    status_.state = AndroidAutoHostState::Connecting;
    status_.endpointAddress.assign(endpoint);
    return Result<void, AndroidAutoError>::success();
}

Result<void, AndroidAutoError> AndroidAutoSession::transportConnected() {
    if (status_.state != AndroidAutoHostState::Connecting &&
        status_.state != AndroidAutoHostState::Reconnecting) {
        return Result<void, AndroidAutoError>::failure(
            AndroidAutoError::InvalidPacket);
    }
    status_.state = AndroidAutoHostState::Negotiating;
    status_.lastError = AndroidAutoError::None;
    return Result<void, AndroidAutoError>::success();
}

Result<void, AndroidAutoError> AndroidAutoSession::versionNegotiated(
    const AndroidAutoProtocolVersion& version) {
    if (status_.state != AndroidAutoHostState::Negotiating ||
        version.major == 0 || version.status != 0) {
        return Result<void, AndroidAutoError>::failure(
            AndroidAutoError::ProtocolMismatch);
    }
    status_.protocolVersion = version;
    return Result<void, AndroidAutoError>::success();
}

Result<void, AndroidAutoError> AndroidAutoSession::negotiated(
    const AndroidAutoCapabilities& capabilities) {
    const auto& video = capabilities.video;
    if (status_.state != AndroidAutoHostState::Negotiating ||
        video.width == 0 || video.height == 0 ||
        video.framesPerSecond == 0 || !capabilities.touch) {
        return Result<void, AndroidAutoError>::failure(
            AndroidAutoError::NegotiationRejected);
    }
    status_.capabilities = capabilities;
    status_.state = AndroidAutoHostState::Streaming;
    status_.lastError = AndroidAutoError::None;
    return Result<void, AndroidAutoError>::success();
}

Result<void, AndroidAutoError> AndroidAutoSession::acceptVideo(
    AndroidAutoVideoPacket packet) {
    if (!isStreaming(status_.state) || packet.data.empty() ||
        packet.data.size() > kMaximumVideoPacketBytes) {
        return Result<void, AndroidAutoError>::failure(
            AndroidAutoError::InvalidPacket);
    }
    pendingVideo_ = std::move(packet);
    ++status_.receivedVideoPackets;
    return Result<void, AndroidAutoError>::success();
}

Result<void, AndroidAutoError> AndroidAutoSession::acceptAudio(
    AndroidAutoAudioPacket packet) {
    if (!isStreaming(status_.state) || packet.sampleRate == 0 ||
        packet.channelCount == 0 || packet.data.empty() ||
        packet.data.size() > kMaximumAudioPacketBytes) {
        return Result<void, AndroidAutoError>::failure(
            AndroidAutoError::InvalidPacket);
    }
    pendingAudio_ = std::move(packet);
    ++status_.receivedAudioPackets;
    return Result<void, AndroidAutoError>::success();
}

Result<void, AndroidAutoError> AndroidAutoSession::validateTouch(
    const AndroidAutoTouchEvent& event) const {
    if (status_.state != AndroidAutoHostState::Streaming ||
        !status_.capabilities.has_value() ||
        !status_.capabilities->touch ||
        !std::isfinite(event.normalizedX) ||
        !std::isfinite(event.normalizedY) ||
        event.normalizedX < 0.0f || event.normalizedX > 1.0f ||
        event.normalizedY < 0.0f || event.normalizedY > 1.0f) {
        return Result<void, AndroidAutoError>::failure(
            AndroidAutoError::InputUnavailable);
    }
    return Result<void, AndroidAutoError>::success();
}

void AndroidAutoSession::suspend() {
    if (status_.state == AndroidAutoHostState::Streaming) {
        status_.state = AndroidAutoHostState::Suspended;
    }
}

void AndroidAutoSession::resume() {
    if (status_.state == AndroidAutoHostState::Suspended) {
        status_.state = AndroidAutoHostState::Streaming;
    }
}

void AndroidAutoSession::transportDisconnected(AndroidAutoError reason) {
    pendingVideo_.reset();
    pendingAudio_.reset();
    if (status_.state == AndroidAutoHostState::Stopped) return;
    status_.state = AndroidAutoHostState::Reconnecting;
    status_.lastError = reason == AndroidAutoError::None
        ? AndroidAutoError::ConnectionClosed : reason;
}

void AndroidAutoSession::fail(AndroidAutoError reason) {
    pendingVideo_.reset();
    pendingAudio_.reset();
    status_.state = AndroidAutoHostState::Failed;
    status_.lastError = reason;
}

void AndroidAutoSession::reset() {
    status_ = {};
    pendingVideo_.reset();
    pendingAudio_.reset();
}

std::optional<AndroidAutoVideoPacket> AndroidAutoSession::takeVideo() {
    auto packet = std::move(pendingVideo_);
    pendingVideo_.reset();
    return packet;
}

std::optional<AndroidAutoAudioPacket> AndroidAutoSession::takeAudio() {
    auto packet = std::move(pendingAudio_);
    pendingAudio_.reset();
    return packet;
}

}  // namespace switch_auto
