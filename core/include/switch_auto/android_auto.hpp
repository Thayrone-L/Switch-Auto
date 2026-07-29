#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "switch_auto/android_auto_protocol.hpp"
#include "switch_auto/result.hpp"

namespace switch_auto {

enum class AndroidAutoHostState {
    Stopped,
    Discovering,
    Connecting,
    Negotiating,
    Streaming,
    Suspended,
    Reconnecting,
    Failed,
};

enum class AndroidAutoError {
    None,
    TransportUnavailable,
    DiscoveryTimeout,
    ConnectionTimeout,
    ConnectionClosed,
    TlsHandshakeFailed,
    ProtocolMismatch,
    NegotiationRejected,
    UnsupportedCodec,
    InvalidPacket,
    DecoderUnavailable,
    AudioUnavailable,
    InputUnavailable,
};

enum class AndroidAutoVideoCodec {
    H264,
    H265,
};

enum class AndroidAutoAudioChannel {
    Media,
    Navigation,
    System,
    Call,
};

enum class AndroidAutoTouchAction {
    Down,
    Move,
    Up,
    Cancel,
};

struct AndroidAutoVideoConfig {
    AndroidAutoVideoCodec codec = AndroidAutoVideoCodec::H264;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t framesPerSecond = 0;
};

struct AndroidAutoCapabilities {
    AndroidAutoVideoConfig video;
    bool touch = false;
    bool mediaAudio = false;
    bool navigationAudio = false;
    bool systemAudio = false;
    bool callAudio = false;
    bool microphone = false;
};

struct AndroidAutoVideoPacket {
    std::uint64_t presentationTimeMicros = 0;
    bool keyFrame = false;
    std::vector<std::uint8_t> data;
};

struct AndroidAutoAudioPacket {
    AndroidAutoAudioChannel channel = AndroidAutoAudioChannel::Media;
    std::uint32_t sampleRate = 0;
    std::uint32_t channelCount = 0;
    std::vector<std::uint8_t> data;
};

struct AndroidAutoTouchEvent {
    AndroidAutoTouchAction action = AndroidAutoTouchAction::Down;
    float normalizedX = 0.0f;
    float normalizedY = 0.0f;
    std::uint64_t timestampMicros = 0;
};

struct AndroidAutoHostStatus {
    AndroidAutoHostState state = AndroidAutoHostState::Stopped;
    AndroidAutoError lastError = AndroidAutoError::None;
    std::string endpointAddress;
    std::optional<AndroidAutoProtocolVersion> protocolVersion;
    std::optional<AndroidAutoCapabilities> capabilities;
    std::uint64_t receivedVideoPackets = 0;
    std::uint64_t receivedAudioPackets = 0;
};

class AndroidAutoHostService {
public:
    using StatusHandler = std::function<void(const AndroidAutoHostStatus&)>;
    using VideoHandler = std::function<void(const AndroidAutoVideoPacket&)>;
    using AudioHandler = std::function<void(const AndroidAutoAudioPacket&)>;

    virtual ~AndroidAutoHostService() = default;
    virtual Result<void, AndroidAutoError> start() = 0;
    virtual void update() = 0;
    virtual void suspend() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    virtual AndroidAutoHostStatus status() const = 0;
    virtual Result<void, AndroidAutoError> sendTouch(
        const AndroidAutoTouchEvent& event) = 0;
    virtual void setStatusHandler(StatusHandler handler) = 0;
    virtual void setVideoHandler(VideoHandler handler) = 0;
    virtual void setAudioHandler(AudioHandler handler) = 0;
};

// Transport-independent lifecycle used by platform implementations. It does not
// parse the Android Auto protocol; it validates transitions and media boundaries.
class AndroidAutoSession {
public:
    Result<void, AndroidAutoError> startDiscovery();
    Result<void, AndroidAutoError> beginConnection(std::string_view endpoint);
    Result<void, AndroidAutoError> transportConnected();
    Result<void, AndroidAutoError> versionNegotiated(
        const AndroidAutoProtocolVersion& version);
    Result<void, AndroidAutoError> negotiated(
        const AndroidAutoCapabilities& capabilities);
    Result<void, AndroidAutoError> acceptVideo(AndroidAutoVideoPacket packet);
    Result<void, AndroidAutoError> acceptAudio(AndroidAutoAudioPacket packet);
    Result<void, AndroidAutoError> validateTouch(
        const AndroidAutoTouchEvent& event) const;
    void suspend();
    void resume();
    void transportDisconnected(AndroidAutoError reason);
    void fail(AndroidAutoError reason);
    void reset();

    const AndroidAutoHostStatus& status() const { return status_; }
    std::optional<AndroidAutoVideoPacket> takeVideo();
    std::optional<AndroidAutoAudioPacket> takeAudio();

private:
    AndroidAutoHostStatus status_;
    std::optional<AndroidAutoVideoPacket> pendingVideo_;
    std::optional<AndroidAutoAudioPacket> pendingAudio_;
};

}  // namespace switch_auto
