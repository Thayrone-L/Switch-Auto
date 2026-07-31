#include "switch_android_auto_host.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <switch.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "switch_auto/android_auto_protocol.hpp"
#include "switch_h264_decoder.hpp"

namespace switch_auto::platform::switch_platform {
namespace {

constexpr std::uint8_t kSensorChannel = 1;
constexpr std::uint8_t kVideoChannel = 2;
constexpr std::uint8_t kInputChannel = 3;
constexpr std::uint8_t kNavigationAudioChannel = 4;
constexpr std::uint8_t kSystemAudioChannel = 5;
constexpr std::uint8_t kMediaAudioChannel = 6;
constexpr std::uint8_t kMicrophoneChannel = 7;
constexpr std::uint32_t kProjectionWidth = 1280;
constexpr std::uint32_t kProjectionHeight = 720;
constexpr std::uint8_t kNightModeSensorType = 10;
constexpr std::uint8_t kDrivingStatusSensorType = 13;

constexpr auto kConnectTimeout = std::chrono::seconds(5);
constexpr auto kPollInterval = std::chrono::milliseconds(100);
constexpr auto kReconnectDelay = std::chrono::seconds(2);
constexpr auto kSecurityFailureDelay = std::chrono::seconds(30);
constexpr auto kVersionTimeout = std::chrono::seconds(5);
constexpr auto kTlsTimeout = std::chrono::seconds(12);
constexpr const char* kSdCertificate =
    "sdmc:/switch/switch-auto/android-auto/headunit.crt";
constexpr const char* kSdPrivateKey =
    "sdmc:/switch/switch-auto/android-auto/headunit.key";
constexpr const char* kRomfsCertificate =
    "romfs:/android-auto/headunit.crt";
constexpr const char* kRomfsPrivateKey =
    "romfs:/android-auto/headunit.key";
constexpr const char* kDiagnosticLog =
    "sdmc:/switch/switch-auto/android-auto.log";

void logTls(std::string_view message) {
    if (std::FILE* output = std::fopen(kDiagnosticLog, "a")) {
        std::fprintf(output, "[AndroidAutoTLS] %.*s\n",
            static_cast<int>(message.size()), message.data());
        std::fclose(output);
    }
}

std::vector<std::uint8_t> makeInitialSensorEvent(
    const std::uint8_t sensorType) {
    // SensorEventIndication (0x8003) contains repeated sensor records.
    // Both initial values are intentionally permissive/static: daytime and
    // unrestricted input. Dynamic vehicle state can replace these later.
    if (sensorType == kNightModeSensorType) {
        // night_mode (field 10) -> NightMode { is_night: false }.
        return {0x80, 0x03, 0x52, 0x02, 0x08, 0x00};
    }
    if (sensorType == kDrivingStatusSensorType) {
        // driving_status (field 13) -> DrivingStatus { status: UNRESTRICTED }.
        return {0x80, 0x03, 0x6a, 0x02, 0x08, 0x00};
    }
    return {};
}

std::string resultText(::Result result) {
    std::ostringstream text;
    text << "0x" << std::hex << static_cast<unsigned long>(result);
    return text.str();
}

bool readCredential(
    const char* path, std::vector<std::uint8_t>& output) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    output.assign(std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
    return !output.empty();
}

bool sendAll(int socketFd, const std::vector<std::uint8_t>& data,
    const std::atomic<bool>& stopRequested) {
    std::size_t offset = 0;
    while (!stopRequested && offset < data.size()) {
        const ssize_t sent = ::send(socketFd, data.data() + offset,
            data.size() - offset, 0);
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) continue;
        return false;
    }
    return offset == data.size();
}

Result<AndroidAutoProtocolVersion, AndroidAutoError> negotiateVersion(
    int socketFd, const std::atomic<bool>& stopRequested) {
    const auto request = makeAndroidAutoVersionRequest();
    if (request.empty() || !sendAll(socketFd, request, stopRequested)) {
        return Result<AndroidAutoProtocolVersion, AndroidAutoError>::failure(
            AndroidAutoError::ConnectionClosed);
    }

    AndroidAutoFrameCodec codec;
    const auto deadline = std::chrono::steady_clock::now() + kVersionTimeout;
    while (!stopRequested && std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{socketFd, POLLIN | POLLHUP | POLLERR, 0};
        const int result = ::poll(
            &descriptor, 1, static_cast<int>(kPollInterval.count()));
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 ||
            (result > 0 &&
                (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)))) {
            return Result<AndroidAutoProtocolVersion,
                AndroidAutoError>::failure(AndroidAutoError::ConnectionClosed);
        }
        if (result <= 0 || !(descriptor.revents & POLLIN)) continue;

        std::uint8_t buffer[4096];
        const ssize_t received = ::recv(socketFd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return Result<AndroidAutoProtocolVersion,
                AndroidAutoError>::failure(AndroidAutoError::ConnectionClosed);
        }
        auto decoded = codec.feed(buffer, static_cast<std::size_t>(received));
        if (!decoded) {
            return Result<AndroidAutoProtocolVersion,
                AndroidAutoError>::failure(AndroidAutoError::ProtocolMismatch);
        }
        for (const auto& frame : decoded.value()) {
            auto version = parseAndroidAutoVersionResponse(frame);
            if (version) {
                return Result<AndroidAutoProtocolVersion,
                    AndroidAutoError>::success(version.value());
            }
            if (version.error() == AndroidAutoProtocolError::VersionRejected) {
                return Result<AndroidAutoProtocolVersion,
                    AndroidAutoError>::failure(
                    AndroidAutoError::NegotiationRejected);
            }
        }
    }
    return Result<AndroidAutoProtocolVersion, AndroidAutoError>::failure(
        AndroidAutoError::ConnectionTimeout);
}

bool createLoopbackPair(int& tlsSocket, int& bridgeSocket) {
    tlsSocket = -1;
    bridgeSocket = -1;
    const int listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0) return false;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0 ||
        ::listen(listener, 1) < 0) {
        ::close(listener);
        return false;
    }
    socklen_t addressSize = sizeof(address);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
            &addressSize) < 0) {
        ::close(listener);
        return false;
    }

    bridgeSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (bridgeSocket < 0 ||
        ::connect(bridgeSocket, reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0) {
        if (bridgeSocket >= 0) ::close(bridgeSocket);
        bridgeSocket = -1;
        ::close(listener);
        return false;
    }
    tlsSocket = ::accept(listener, nullptr, nullptr);
    ::close(listener);
    if (tlsSocket < 0) {
        ::close(bridgeSocket);
        bridgeSocket = -1;
        return false;
    }
    return true;
}

void appendVarint(std::vector<std::uint8_t>& target, std::uint64_t value) {
    while (value >= 0x80) {
        target.push_back(
            static_cast<std::uint8_t>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    target.push_back(static_cast<std::uint8_t>(value));
}

void appendProtoVarint(std::vector<std::uint8_t>& target,
    std::uint32_t field, std::uint64_t value) {
    appendVarint(target, static_cast<std::uint64_t>(field) << 3);
    appendVarint(target, value);
}

void appendProtoBytes(std::vector<std::uint8_t>& target,
    std::uint32_t field, const std::vector<std::uint8_t>& value) {
    appendVarint(target,
        (static_cast<std::uint64_t>(field) << 3) | 2);
    appendVarint(target, value.size());
    target.insert(target.end(), value.begin(), value.end());
}

void appendProtoString(std::vector<std::uint8_t>& target,
    std::uint32_t field, std::string_view value) {
    appendVarint(target,
        (static_cast<std::uint64_t>(field) << 3) | 2);
    appendVarint(target, value.size());
    target.insert(target.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> makeTouchEventMessage(
    const AndroidAutoTouchEvent& event) {
    const auto x = static_cast<std::uint32_t>(std::lround(
        std::clamp(event.normalizedX, 0.0f, 1.0f) *
        static_cast<float>(kProjectionWidth - 1)));
    const auto y = static_cast<std::uint32_t>(std::lround(
        std::clamp(event.normalizedY, 0.0f, 1.0f) *
        static_cast<float>(kProjectionHeight - 1)));

    std::vector<std::uint8_t> location;
    appendProtoVarint(location, 1, x);
    appendProtoVarint(location, 2, y);
    // Android Auto's current proto2 schema marks pointer_id as required.
    // It must be present on the wire even for the primary pointer (zero).
    appendProtoVarint(location, 3, 0);

    std::vector<std::uint8_t> touch;
    appendProtoBytes(touch, 1, location);
    // Match the current schema explicitly. Zero identifies the first pointer.
    appendProtoVarint(touch, 2, 0);
    std::uint32_t action = 0;
    if (event.action == AndroidAutoTouchAction::Up ||
        event.action == AndroidAutoTouchAction::Cancel) {
        action = 1;
    } else if (event.action == AndroidAutoTouchAction::Move) {
        action = 2;
    }
    appendProtoVarint(touch, 3, action);

    std::vector<std::uint8_t> indication;
    // Current Android Auto hosts report input timestamps in nanoseconds.
    // Keep the public Core event in microseconds and convert at the protocol
    // boundary so older callers remain source-compatible.
    appendProtoVarint(indication, 1, event.timestampMicros * 1000ULL);
    appendProtoBytes(indication, 3, touch);

    std::vector<std::uint8_t> message{0x80, 0x01};
    message.insert(message.end(), indication.begin(), indication.end());
    return message;
}

std::vector<std::uint8_t> makeServiceDiscoveryResponsePayload() {
    // Android Auto 1.x uses the classic ChannelDescriptor schema.
    // Channel 1: driving-status and night-mode sensors.
    std::vector<std::uint8_t> drivingStatusSensor;
    appendProtoVarint(drivingStatusSensor, 1, 13);
    std::vector<std::uint8_t> nightModeSensor;
    appendProtoVarint(nightModeSensor, 1, 10);
    std::vector<std::uint8_t> sensorSource;
    appendProtoBytes(sensorSource, 1, drivingStatusSensor);
    appendProtoBytes(sensorSource, 1, nightModeSensor);
    std::vector<std::uint8_t> sensorChannel;
    appendProtoVarint(sensorChannel, 1, 1);
    appendProtoBytes(sensorChannel, 2, sensorSource);

    // Channel 2: H.264 video sink, 1280x720 at a stable 30 fps.
    std::vector<std::uint8_t> videoConfiguration;
    appendProtoVarint(videoConfiguration, 1, 2);  // 1280x720
    appendProtoVarint(videoConfiguration, 2, 1);  // VideoFPS::_30
    appendProtoVarint(videoConfiguration, 5, 160);
    appendProtoVarint(videoConfiguration, 6, 1);  // One extra decoder frame

    std::vector<std::uint8_t> mediaSink;
    appendProtoVarint(mediaSink, 1, 3);  // VIDEO
    appendProtoBytes(mediaSink, 4, videoConfiguration);
    appendProtoVarint(mediaSink, 5, 1);  // Available during calls

    std::vector<std::uint8_t> videoChannel;
    appendProtoVarint(videoChannel, 1, 2);
    appendProtoBytes(videoChannel, 3, mediaSink);

    auto makeAudioSinkChannel = [](std::uint32_t id,
                                    std::uint32_t audioType,
                                    std::uint32_t sampleRate,
                                    std::uint32_t channels) {
        std::vector<std::uint8_t> audioConfiguration;
        appendProtoVarint(audioConfiguration, 1, sampleRate);
        appendProtoVarint(audioConfiguration, 2, 16);
        appendProtoVarint(audioConfiguration, 3, channels);

        std::vector<std::uint8_t> audioSink;
        appendProtoVarint(audioSink, 1, 1);  // AUDIO
        appendProtoVarint(audioSink, 2, audioType);
        appendProtoBytes(audioSink, 3, audioConfiguration);
        appendProtoVarint(audioSink, 5, 1);

        std::vector<std::uint8_t> channel;
        appendProtoVarint(channel, 1, id);
        appendProtoBytes(channel, 3, audioSink);
        return channel;
    };

    // Android Auto requires these sinks to establish projection on current
    // phones. Switch Auto acknowledges and discards their PCM payloads: media
    // control remains in AA while playback is intentionally not implemented.
    const auto mediaAudioChannel =
        makeAudioSinkChannel(kMediaAudioChannel, 3, 48000, 2);
    const auto navigationAudioChannel =
        makeAudioSinkChannel(kNavigationAudioChannel, 1, 16000, 1);
    const auto systemAudioChannel =
        makeAudioSinkChannel(kSystemAudioChannel, 2, 16000, 1);

    // Channel 3: capacitive touchscreen matching the projected display.
    std::vector<std::uint8_t> touchscreen;
    appendProtoVarint(touchscreen, 1, kProjectionWidth);
    appendProtoVarint(touchscreen, 2, kProjectionHeight);

    std::vector<std::uint8_t> inputSource;
    appendProtoBytes(inputSource, 2, touchscreen);

    std::vector<std::uint8_t> inputChannel;
    appendProtoVarint(inputChannel, 1, kInputChannel);
    appendProtoBytes(inputChannel, 4, inputSource);

    // Android Auto requires a microphone source in the discovery contract on
    // current phones. It is advertised for protocol compatibility only;
    // Switch Auto does not capture or forward microphone samples.
    std::vector<std::uint8_t> microphoneConfiguration;
    appendProtoVarint(microphoneConfiguration, 1, 16000);
    appendProtoVarint(microphoneConfiguration, 2, 16);
    appendProtoVarint(microphoneConfiguration, 3, 1);
    std::vector<std::uint8_t> microphoneSource;
    appendProtoVarint(microphoneSource, 1, 1);  // AUDIO
    appendProtoBytes(microphoneSource, 2, microphoneConfiguration);
    appendProtoVarint(microphoneSource, 3, 1);
    std::vector<std::uint8_t> microphoneChannel;
    appendProtoVarint(microphoneChannel, 1, kMicrophoneChannel);
    appendProtoBytes(microphoneChannel, 5, microphoneSource);

    std::vector<std::uint8_t> response;
    appendProtoBytes(response, 1, sensorChannel);
    appendProtoBytes(response, 1, videoChannel);
    appendProtoBytes(response, 1, mediaAudioChannel);
    appendProtoBytes(response, 1, navigationAudioChannel);
    appendProtoBytes(response, 1, systemAudioChannel);
    appendProtoBytes(response, 1, inputChannel);
    appendProtoBytes(response, 1, microphoneChannel);
    appendProtoString(response, 2, "Switch Auto");
    appendProtoString(response, 3, "Nintendo Switch");
    appendProtoString(response, 4, "2026");
    appendProtoString(response, 5, "SWITCH-AUTO-DEV");
    appendProtoVarint(response, 6, 1);  // Left-hand-drive vehicle
    appendProtoString(response, 7, "Switch Auto");
    appendProtoString(response, 8, "Nintendo Switch");
    appendProtoString(response, 9, "dev");
    appendProtoString(response, 10, "0.1.0");
    return response;
}

bool sendEncryptedControl(SslConnection& connection, int bridgeSocket,
    int phoneSocket, std::uint8_t channel,
    const std::vector<std::uint8_t>& plaintext,
    const std::atomic<bool>& stopRequested,
    bool controlMessage = false) {
    u32 written = 0;
    const ::Result writeResult = sslConnectionWrite(&connection,
        plaintext.data(), static_cast<u32>(plaintext.size()), &written);
    if (R_FAILED(writeResult) || written != plaintext.size()) {
        logTls("encrypted write failed result=" + resultText(writeResult));
        return false;
    }

    std::vector<std::uint8_t> ciphertext;
    // sslConnectionWrite emits the complete TLS record into the local bridge.
    // Wait only for its first bytes, then drain everything already available.
    // The old 10 ms "maybe more data" poll ran once per video ACK and consumed
    // most of the transport thread at 60 fps, starving touch messages.
    while (true) {
        pollfd descriptor{bridgeSocket, POLLIN, 0};
        const int result = ::poll(&descriptor, 1,
            ciphertext.empty() ? 100 : 0);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0 || !(descriptor.revents & POLLIN)) break;
        std::uint8_t buffer[16384];
        const ssize_t received =
            ::recv(bridgeSocket, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (received <= 0) return false;
        ciphertext.insert(
            ciphertext.end(), buffer, buffer + received);
    }
    if (ciphertext.empty()) return false;

    AndroidAutoFrame frame;
    frame.channel = channel;
    frame.type = AndroidAutoFrameType::Bulk;
    frame.encrypted = true;
    frame.controlMessage = controlMessage;
    frame.payload = std::move(ciphertext);
    auto encoded = AndroidAutoFrameCodec::encode(frame);
    return encoded && sendAll(phoneSocket, encoded.value(), stopRequested);
}

using TouchSource =
    std::function<std::optional<AndroidAutoTouchEvent>()>;

class AsyncVideoDecoder {
public:
    AsyncVideoDecoder() : worker_([this]() { run(); }) {}

    ~AsyncVideoDecoder() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            packets_.clear();
        }
        ready_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    bool available() const {
        return decoder_.available();
    }

    bool enqueue(const std::uint8_t* data, std::size_t size) {
        if (!available() || !data || size == 0) return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Never discard compressed H.264 reference data. A pathological
            // backlog is safer to recover by reconnecting than by displaying a
            // corrupted stream until the next keyframe.
            if (packets_.size() >= 256) return false;
            std::vector<std::uint8_t> packet;
            if (!recycledPackets_.empty()) {
                packet = std::move(recycledPackets_.back());
                recycledPackets_.pop_back();
            }
            packet.assign(data, data + size);
            packets_.push_back(std::move(packet));
            queueHighWater_ = std::max(queueHighWater_, packets_.size());
        }
        ready_.notify_one();
        return true;
    }

    std::tuple<std::uint64_t, std::uint64_t, std::uint64_t> metrics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {decodedPackets_, totalDecodeMicros_, queueHighWater_};
    }

private:
    void run() {
        while (true) {
            std::vector<std::uint8_t> packet;
            bool present = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock,
                    [this]() { return stopping_ || !packets_.empty(); });
                if (stopping_ && packets_.empty()) return;
                packet = std::move(packets_.front());
                packets_.pop_front();
                busy_ = true;
                const auto now = std::chrono::steady_clock::now();
                present = packets_.empty() ||
                    now - lastPresentation_ >=
                        std::chrono::milliseconds(33);
                if (present) lastPresentation_ = now;
            }
            const auto decodeStarted = std::chrono::steady_clock::now();
            decoder_.decode(packet.data(), packet.size(), present);
            const auto decodeMicros =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - decodeStarted)
                    .count();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                busy_ = false;
                ++decodedPackets_;
                totalDecodeMicros_ +=
                    static_cast<std::uint64_t>(decodeMicros);
                packet.clear();
                if (recycledPackets_.size() < 2) {
                    recycledPackets_.push_back(std::move(packet));
                }
            }
            completed_.notify_all();
        }
    }

    SwitchH264Decoder decoder_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::condition_variable completed_;
    std::deque<std::vector<std::uint8_t>> packets_;
    std::vector<std::vector<std::uint8_t>> recycledPackets_;
    bool busy_ = false;
    bool stopping_ = false;
    std::uint64_t decodedPackets_ = 0;
    std::uint64_t totalDecodeMicros_ = 0;
    std::uint64_t queueHighWater_ = 0;
    std::chrono::steady_clock::time_point lastPresentation_{};
    std::thread worker_;
};

bool runServiceDiscovery(SslConnection& connection, int bridgeSocket,
    int phoneSocket, const std::atomic<bool>& stopRequested,
    const TouchSource& touchSource) {
    AndroidAutoFrameCodec codec;
    bool discoverySent = false;
    std::array<std::uint8_t, 256> mediaSessions{};
    std::array<std::uint64_t, 256> mediaPacketCounts{};
    std::uint64_t videoPackets = 0;
    const auto videoStartedAt = std::chrono::steady_clock::now();
    bool inputBound = false;
    std::array<std::vector<std::uint8_t>, 256> fragmentedPlaintext;
    AsyncVideoDecoder videoDecoder;
    if (!videoDecoder.available()) {
        logTls("FFmpeg H264 decoder unavailable");
    }
    auto drainTouches = [&]() {
        if (!inputBound) return true;
        // Keep input responsive without starving 60 fps video reads. A burst
        // of taps is spread over consecutive transport iterations.
        constexpr std::size_t kMaxTouchesPerIteration = 4;
        for (std::size_t sent = 0;
             sent < kMaxTouchesPerIteration && touchSource; ++sent) {
            auto touch = touchSource();
            if (!touch) return true;
            const auto message = makeTouchEventMessage(*touch);
            if (!sendEncryptedControl(connection, bridgeSocket, phoneSocket,
                    kInputChannel, message, stopRequested)) {
                return false;
            }
        }
        return true;
    };
    while (!stopRequested) {
        if (!drainTouches()) return false;
        pollfd descriptor{
            phoneSocket, POLLIN | POLLHUP | POLLERR, 0};
        const int pollResult = ::poll(
            &descriptor, 1, static_cast<int>(kPollInterval.count()));
        if (pollResult < 0 && errno == EINTR) continue;
        if (pollResult < 0 ||
            (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL))) {
            return false;
        }
        if (!(descriptor.revents & POLLIN)) continue;

        std::uint8_t protocolBytes[16384];
        const ssize_t received = ::recv(
            phoneSocket, protocolBytes, sizeof(protocolBytes), 0);
        if (received <= 0) return false;
        auto frames =
            codec.feed(protocolBytes, static_cast<std::size_t>(received));
        if (!frames) return false;

        for (const auto& frame : frames.value()) {
            if (!drainTouches()) return false;
            if (!frame.encrypted ||
                !sendAll(bridgeSocket, frame.payload, stopRequested)) {
                logTls("unexpected post-auth frame");
                return false;
            }

            std::uint8_t plaintextBuffer[32768];
            u32 plaintextSize = 0;
            const ::Result readResult = sslConnectionRead(&connection,
                plaintextBuffer, sizeof(plaintextBuffer), &plaintextSize);
            if (R_FAILED(readResult)) {
                logTls("encrypted read failed result=" +
                    resultText(readResult));
                return false;
            }
            auto& assembly = fragmentedPlaintext[frame.channel];
            if (frame.type == AndroidAutoFrameType::First) {
                assembly.assign(
                    plaintextBuffer, plaintextBuffer + plaintextSize);
                continue;
            }
            if (frame.type == AndroidAutoFrameType::Middle) {
                assembly.insert(
                    assembly.end(), plaintextBuffer,
                    plaintextBuffer + plaintextSize);
                continue;
            }

            std::vector<std::uint8_t> assembledMessage;
            const std::uint8_t* messageBytes = plaintextBuffer;
            std::size_t messageSize = plaintextSize;
            if (frame.type == AndroidAutoFrameType::Last) {
                assembly.insert(
                    assembly.end(), plaintextBuffer,
                    plaintextBuffer + plaintextSize);
                assembledMessage = std::move(assembly);
                assembly.clear();
                messageBytes = assembledMessage.data();
                messageSize = assembledMessage.size();
            }
            if (messageSize < 2) {
                logTls("decrypted message too short");
                return false;
            }
            const std::uint16_t messageId =
                (static_cast<std::uint16_t>(messageBytes[0]) << 8) |
                messageBytes[1];
            // Media packets arrive up to 30 times per second. Logging each one
            // opens and flushes the SD-card file synchronously, which causes
            // visible frame stalls and can starve the projection transport.
            // Video/audio already publish compact aggregate metrics below.
            if (messageId != 0 && messageId != 1 && messageId != 18) {
                logTls("received encrypted control message=" +
                    std::to_string(messageId) + " channel=" +
                    std::to_string(frame.channel));
            }

            if (frame.channel == 0 && messageId == 5 && !discoverySent) {
                std::vector<std::uint8_t> response{0x00, 0x06};
                const auto payload = makeServiceDiscoveryResponsePayload();
                response.insert(
                    response.end(), payload.begin(), payload.end());
                if (!sendEncryptedControl(connection, bridgeSocket,
                        phoneSocket, 0, response, stopRequested)) {
                    return false;
                }
                discoverySent = true;
                logTls("service discovery response sent video_channel=" +
                    std::to_string(kVideoChannel) + " input_channel=" +
                    std::to_string(kInputChannel));
            } else if (frame.channel != 0 && messageId == 7) {
                // ChannelOpenResponse { status: OK }.
                const std::vector<std::uint8_t> response{
                    0x00, 0x08, 0x08, 0x00};
                if (!sendEncryptedControl(connection, bridgeSocket,
                        phoneSocket, frame.channel, response,
                        stopRequested, true)) {
                    return false;
                }
                logTls("channel open response sent channel=" +
                    std::to_string(frame.channel));
            } else if (frame.channel == kSensorChannel &&
                messageId == 0x8001) {
                // SensorStartRequest { sensor_type: ... }. The two sensors we
                // advertise currently fit in one-byte protobuf varints.
                const std::uint8_t sensorType =
                    messageSize >= 4 && messageBytes[2] == 0x08
                    ? messageBytes[3]
                    : 0;
                // SensorStartResponse { status: OK }.
                const std::vector<std::uint8_t> response{
                    0x80, 0x02, 0x08, 0x00};
                if (!sendEncryptedControl(connection, bridgeSocket,
                        phoneSocket, frame.channel, response,
                        stopRequested)) {
                    return false;
                }
                const auto initialEvent = makeInitialSensorEvent(sensorType);
                if (!initialEvent.empty() &&
                    !sendEncryptedControl(connection, bridgeSocket,
                        phoneSocket, frame.channel, initialEvent,
                        stopRequested)) {
                    return false;
                }
                logTls("sensor start response sent type=" +
                    std::to_string(sensorType) +
                    (initialEvent.empty() ? " event=unsupported"
                                          : " event=initial"));
            } else if (frame.channel == kInputChannel &&
                messageId == 0x8002) {
                // BindingResponse { status: OK }.
                const std::vector<std::uint8_t> response{
                    0x80, 0x03, 0x08, 0x00};
                if (!sendEncryptedControl(connection, bridgeSocket,
                        phoneSocket, frame.channel, response,
                        stopRequested)) {
                    return false;
                }
                inputBound = true;
                logTls("input binding accepted");
            } else if (frame.channel >= 2 && frame.channel <= 7 &&
                messageId == 0x8000) {
                // AVChannelSetupResponse {
                //   media_status: OK, max_unacked: 1, configs: 0
                // }.
                const std::vector<std::uint8_t> response{
                    0x80, 0x03, 0x08, 0x02, 0x10, 0x01, 0x18, 0x00};
                if (!sendEncryptedControl(connection, bridgeSocket,
                        phoneSocket, frame.channel, response,
                        stopRequested)) {
                    return false;
                }
                logTls("AV setup response sent channel=" +
                    std::to_string(frame.channel));

                if (frame.channel == kVideoChannel) {
                    // VideoFocusIndication { focus_mode: FOCUSED }.
                    const std::vector<std::uint8_t> focus{
                        0x80, 0x08, 0x08, 0x01};
                    if (!sendEncryptedControl(connection, bridgeSocket,
                            phoneSocket, frame.channel, focus,
                            stopRequested)) {
                        return false;
                    }
                    logTls("video focus indication sent");
                }
            } else if (frame.channel == kVideoChannel &&
                messageId == 0x8007) {
                const std::vector<std::uint8_t> focus{
                    0x80, 0x08, 0x08, 0x01};
                if (!sendEncryptedControl(connection, bridgeSocket,
                        phoneSocket, frame.channel, focus,
                        stopRequested)) {
                    return false;
                }
                logTls("video focus request accepted");
            } else if (frame.channel >= 2 && frame.channel <= 7 &&
                messageId == 0x8001) {
                // AVChannelStartIndication. Session is protobuf field 1 and is
                // normally encoded as a one-byte varint.
                if (messageSize >= 4 && messageBytes[2] == 0x08) {
                    mediaSessions[frame.channel] = messageBytes[3];
                }
                logTls("AV channel started channel=" +
                    std::to_string(frame.channel) + " session=" +
                    std::to_string(mediaSessions[frame.channel]));
            } else if (frame.channel == kMicrophoneChannel &&
                messageId == 0x8005) {
                // AVInputOpenResponse { session: 0, value: 0 }.
                const std::vector<std::uint8_t> response{
                    0x80, 0x06, 0x08, 0x00, 0x10, 0x00};
                if (!sendEncryptedControl(connection, bridgeSocket,
                        phoneSocket, frame.channel, response,
                        stopRequested)) {
                    return false;
                }
                logTls("microphone open request acknowledged");
            } else if (frame.channel == kVideoChannel &&
                (messageId == 0x0000 || messageId == 0x0001)) {
                ++videoPackets;
                const std::size_t mediaOffset =
                    messageId == 0x0000 ? 10 : 2;
                // Release the phone immediately; decode runs independently.
                const std::vector<std::uint8_t> ack{
                    0x80, 0x04, 0x08,
                    mediaSessions[kVideoChannel], 0x10, 0x01};
                if (!sendEncryptedControl(connection, bridgeSocket,
                        phoneSocket, frame.channel, ack,
                        stopRequested)) {
                    return false;
                }
                if (messageSize > mediaOffset && videoDecoder.available()) {
                    if (!videoDecoder.enqueue(messageBytes + mediaOffset,
                            messageSize - mediaOffset)) {
                        logTls("video decoder queue overflow");
                        return false;
                    }
                }
                if (videoPackets == 1 || videoPackets % 60 == 0) {
                    const double elapsedSeconds =
                        std::chrono::duration<double>(
                            std::chrono::steady_clock::now() -
                            videoStartedAt)
                            .count();
                    const auto [decodedPackets, totalDecodeMicros,
                        queueHighWater] =
                        videoDecoder.metrics();
                    const double receiveFps = elapsedSeconds > 0.0
                        ? static_cast<double>(videoPackets) / elapsedSeconds
                        : 0.0;
                    const double averageDecodeMs = decodedPackets > 0
                        ? static_cast<double>(totalDecodeMicros) /
                            static_cast<double>(decodedPackets) / 1000.0
                        : 0.0;
                    logTls("H264 packets received=" +
                        std::to_string(videoPackets) + " bytes=" +
                        std::to_string(messageSize - 2) +
                        " receive_fps=" + std::to_string(receiveFps) +
                        " decoded_packets=" +
                        std::to_string(decodedPackets) +
                        " queue_high_water=" +
                        std::to_string(queueHighWater) +
                        " avg_decode_ms=" +
                        std::to_string(averageDecodeMs));
                }
            } else if (frame.channel >= kNavigationAudioChannel &&
                frame.channel <= kMediaAudioChannel &&
                (messageId == 0x0000 || messageId == 0x0001)) {
                // Keep audio channels flowing even before PCM playback is
                // implemented. Without an ACK, Spotify/system audio stalls and
                // Android eventually closes the entire projection session.
                ++mediaPacketCounts[frame.channel];
                const std::vector<std::uint8_t> ack{
                    0x80, 0x04, 0x08, mediaSessions[frame.channel],
                    0x10, 0x01};
                if (!sendEncryptedControl(connection, bridgeSocket,
                        phoneSocket, frame.channel, ack,
                        stopRequested)) {
                    return false;
                }
                if (mediaPacketCounts[frame.channel] == 1 ||
                    mediaPacketCounts[frame.channel] % 250 == 0) {
                    logTls("audio packets acknowledged channel=" +
                        std::to_string(frame.channel) + " packets=" +
                        std::to_string(mediaPacketCounts[frame.channel]));
                }
            }
        }
    }
    return discoverySent;
}

AndroidAutoError performTlsHandshake(int phoneSocket,
    const std::atomic<bool>& stopRequested,
    const TouchSource& touchSource) {
    logTls("initializing");
    const ::Result initializeResult = sslInitialize(1);
    if (R_FAILED(initializeResult)) {
        logTls("sslInitialize failed result=" + resultText(initializeResult));
        return AndroidAutoError::TransportUnavailable;
    }

    SslContext context{};
    SslConnection connection{};
    bool contextOpen = false;
    bool connectionOpen = false;
    int tlsSocket = -1;
    int bridgeSocket = -1;
    AndroidAutoError outcome = AndroidAutoError::ProtocolMismatch;

    do {
        const ::Result contextResult = sslCreateContext(
            &context, SslVersion_TlsV10 | SslVersion_TlsV11 |
                SslVersion_TlsV12);
        if (R_FAILED(contextResult)) {
            logTls("sslCreateContext failed result=" +
                resultText(contextResult));
            break;
        }
        contextOpen = true;

        std::vector<std::uint8_t> certificate;
        std::vector<std::uint8_t> privateKey;
        const char* certificateSource = nullptr;
        if (readCredential(kSdCertificate, certificate) &&
            readCredential(kSdPrivateKey, privateKey)) {
            certificateSource = "sdmc";
        } else if (readCredential(kRomfsCertificate, certificate) &&
            readCredential(kRomfsPrivateKey, privateKey)) {
            certificateSource = "romfs";
        }

        ::Result credentialResult = 0;
        if (certificateSource != nullptr) {
            credentialResult = sslContextImportClientCertKeyPki(&context,
                certificate.data(), static_cast<u32>(certificate.size()),
                privateKey.data(), static_cast<u32>(privateKey.size()),
                SslCertificateFormat_Pem, nullptr);
            logTls(std::string("credential source=") + certificateSource);
        } else {
            certificate.resize(8192);
            privateKey.resize(8192);
            u32 certificateSize = 0;
            u32 privateKeySize = 0;
            SslKeyAndCertParams parameters{};
            parameters.unk_x0 = 1;
            parameters.key_size = 2048;
            parameters.public_exponent = 65537;
            constexpr char kCommonName[] = "Switch Auto Head Unit";
            std::memcpy(
                parameters.common_name, kCommonName, sizeof(kCommonName));
            parameters.common_name_len = sizeof(kCommonName) - 1;
            credentialResult = sslContextGeneratePrivateKeyAndCert(&context,
                certificate.data(), static_cast<u32>(certificate.size()),
                privateKey.data(), static_cast<u32>(privateKey.size()), 1,
                &parameters, &certificateSize, &privateKeySize);
            if (R_SUCCEEDED(credentialResult)) {
                credentialResult = sslContextImportClientCertKeyPki(&context,
                    certificate.data(), certificateSize, privateKey.data(),
                    privateKeySize, SslCertificateFormat_Der, nullptr);
            }
            logTls("credential source=self-signed");
        }
        if (R_FAILED(credentialResult)) {
            logTls("credential import failed result=" +
                resultText(credentialResult));
            break;
        }

        if (!createLoopbackPair(tlsSocket, bridgeSocket)) {
            logTls("loopback bridge creation failed");
            break;
        }
        const ::Result connectionResult =
            sslContextCreateConnection(&context, &connection);
        if (R_FAILED(connectionResult)) {
            logTls("sslContextCreateConnection failed result=" +
                resultText(connectionResult));
            break;
        }
        connectionOpen = true;
        const ::Result skipVerifyResult = sslConnectionSetOption(
            &connection, SslOptionType_SkipDefaultVerify, true);
        const ::Result verifyResult =
            R_SUCCEEDED(skipVerifyResult)
            ? sslConnectionSetVerifyOption(&connection, 0)
            : skipVerifyResult;
        const int socketResult = R_SUCCEEDED(verifyResult)
            ? socketSslConnectionSetSocketDescriptor(&connection, tlsSocket)
            : -1;
        if (R_FAILED(skipVerifyResult) || R_FAILED(verifyResult) ||
            socketResult < 0) {
            logTls("connection setup failed option=" +
                resultText(skipVerifyResult) + " verify=" +
                resultText(verifyResult) + " socket=" +
                std::to_string(socketResult));
            break;
        }
        tlsSocket = -1;  // Ownership was transferred to libnx.

        std::atomic<bool> handshakeFinished{false};
        std::atomic<bool> handshakeSucceeded{false};
        std::atomic<::Result> handshakeResult{0};
        std::thread handshakeThread([&]() {
            handshakeResult = sslConnectionDoHandshake(
                &connection, nullptr, nullptr, nullptr, 0);
            handshakeSucceeded = R_SUCCEEDED(handshakeResult);
            handshakeFinished = true;
        });

        AndroidAutoFrameCodec codec;
        const auto deadline = std::chrono::steady_clock::now() + kTlsTimeout;
        bool bridgeFailed = false;
        while (!stopRequested && !bridgeFailed &&
            std::chrono::steady_clock::now() < deadline) {
            pollfd descriptors[2]{
                {phoneSocket, POLLIN | POLLHUP | POLLERR, 0},
                {bridgeSocket, POLLIN | POLLHUP | POLLERR, 0}};
            const int pollResult = ::poll(
                descriptors, 2, static_cast<int>(kPollInterval.count()));
            if (pollResult < 0 && errno == EINTR) continue;
            if (pollResult < 0) {
                bridgeFailed = true;
                break;
            }

            if (descriptors[1].revents & POLLIN) {
                std::uint8_t tlsBytes[16384];
                const ssize_t received =
                    ::recv(bridgeSocket, tlsBytes, sizeof(tlsBytes), 0);
                const auto packet = received > 0
                    ? makeAndroidAutoTlsHandshake(
                          tlsBytes, static_cast<std::size_t>(received))
                    : std::vector<std::uint8_t>{};
                if (received <= 0 ||
                    !sendAll(phoneSocket, packet, stopRequested)) {
                    bridgeFailed = true;
                }
            }
            if (descriptors[0].revents & POLLIN) {
                std::uint8_t protocolBytes[16384];
                const ssize_t received = ::recv(
                    phoneSocket, protocolBytes, sizeof(protocolBytes), 0);
                if (received <= 0) {
                    bridgeFailed = true;
                } else {
                    auto frames = codec.feed(protocolBytes,
                        static_cast<std::size_t>(received));
                    if (!frames) {
                        bridgeFailed = true;
                    } else {
                        for (const auto& frame : frames.value()) {
                            auto tls = parseAndroidAutoTlsHandshake(frame);
                            if (!tls || !sendAll(
                                    bridgeSocket, tls.value(), stopRequested)) {
                                bridgeFailed = true;
                                break;
                            }
                        }
                    }
                }
            }
            if ((descriptors[0].revents | descriptors[1].revents) &
                (POLLHUP | POLLERR | POLLNVAL)) {
                bridgeFailed = true;
            }
            if (handshakeFinished && handshakeSucceeded && !bridgeFailed) {
                outcome = AndroidAutoError::None;
                break;
            }
            if (handshakeFinished && !handshakeSucceeded) {
                bridgeFailed = true;
            }
        }

        if (outcome != AndroidAutoError::None) {
            outcome = stopRequested ? AndroidAutoError::ConnectionClosed
                                    : AndroidAutoError::ConnectionTimeout;
            ::shutdown(bridgeSocket, SHUT_RDWR);
        }
        if (handshakeThread.joinable()) handshakeThread.join();
        logTls(std::string("handshake ") +
            (handshakeSucceeded ? "complete" : "failed") +
            " result=" + resultText(handshakeResult));
        if (outcome == AndroidAutoError::None &&
            !sendAll(phoneSocket, makeAndroidAutoAuthComplete(),
                stopRequested)) {
            outcome = AndroidAutoError::ConnectionClosed;
        } else if (outcome == AndroidAutoError::None) {
            logTls("auth complete sent");
            if (!runServiceDiscovery(
                    connection, bridgeSocket, phoneSocket, stopRequested,
                    touchSource)) {
                outcome = AndroidAutoError::ConnectionClosed;
            }
        }
    } while (false);

    if (bridgeSocket >= 0) ::close(bridgeSocket);
    if (tlsSocket >= 0) ::close(tlsSocket);
    if (connectionOpen) sslConnectionClose(&connection);
    if (contextOpen) sslContextClose(&context);
    sslExit();
    return outcome;
}

int connectSocket(std::string_view address, std::uint16_t port,
    const std::atomic<bool>& stopRequested, AndroidAutoError& failure) {
    failure = AndroidAutoError::TransportUnavailable;
    const int socketFd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketFd < 0) return -1;

    const int originalFlags = ::fcntl(socketFd, F_GETFL, 0);
    if (originalFlags < 0 ||
        ::fcntl(socketFd, F_SETFL, originalFlags | O_NONBLOCK) < 0) {
        ::close(socketFd);
        return -1;
    }

    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    const std::string addressText(address);
    if (::inet_pton(AF_INET, addressText.c_str(), &endpoint.sin_addr) != 1) {
        ::close(socketFd);
        failure = AndroidAutoError::DiscoveryTimeout;
        return -1;
    }

    if (::connect(socketFd, reinterpret_cast<sockaddr*>(&endpoint),
            sizeof(endpoint)) < 0 && errno != EINPROGRESS) {
        ::close(socketFd);
        return -1;
    }

    const auto deadline = std::chrono::steady_clock::now() + kConnectTimeout;
    bool connected = false;
    while (!stopRequested && std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{socketFd, POLLOUT, 0};
        const int result = ::poll(
            &descriptor, 1, static_cast<int>(kPollInterval.count()));
        if (result < 0 && errno != EINTR) break;
        if (result <= 0) continue;
        int socketError = 0;
        socklen_t errorLength = sizeof(socketError);
        if (::getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &socketError,
                &errorLength) == 0 && socketError == 0) {
            connected = true;
        }
        break;
    }

    if (!connected && !stopRequested &&
        std::chrono::steady_clock::now() >= deadline) {
        failure = AndroidAutoError::ConnectionTimeout;
    }
    if (!connected || stopRequested ||
        ::fcntl(socketFd, F_SETFL, originalFlags) < 0) {
        ::close(socketFd);
        return -1;
    }
    return socketFd;
}

}  // namespace

SwitchAndroidAutoHost::SwitchAndroidAutoHost(
    EventQueue& events, EndpointProvider endpointProvider)
    : events_(events), endpointProvider_(std::move(endpointProvider)) {}

SwitchAndroidAutoHost::~SwitchAndroidAutoHost() {
    stop();
}

Result<void, AndroidAutoError> SwitchAndroidAutoHost::start() {
    if (worker_.joinable()) {
        return Result<void, AndroidAutoError>::success();
    }
    stopRequested_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.reset();
        const auto result = session_.startDiscovery();
        if (!result) return result;
    }
    publish();
    worker_ = std::thread(&SwitchAndroidAutoHost::run, this);
    return Result<void, AndroidAutoError>::success();
}

void SwitchAndroidAutoHost::suspend() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.suspend();
    }
    publish();
}

void SwitchAndroidAutoHost::resume() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.resume();
    }
    publish();
}

void SwitchAndroidAutoHost::stop() {
    if (stopRequested_ && !worker_.joinable()) return;
    stopRequested_ = true;
    stopCondition_.notify_all();
    disconnectSocket();
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.reset();
    }
    publish();
}

AndroidAutoHostStatus SwitchAndroidAutoHost::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_.status();
}

Result<void, AndroidAutoError> SwitchAndroidAutoHost::sendTouch(
    const AndroidAutoTouchEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto validation = session_.validateTouch(event);
    if (!validation) return validation;
    if (event.action == AndroidAutoTouchAction::Move &&
        !pendingTouches_.empty() &&
        pendingTouches_.back().action == AndroidAutoTouchAction::Move) {
        pendingTouches_.back() = event;
    } else {
        pendingTouches_.push_back(event);
    }
    constexpr std::size_t kMaxPendingTouches = 16;
    while (pendingTouches_.size() > kMaxPendingTouches) {
        auto move = std::find_if(pendingTouches_.begin(),
            pendingTouches_.end(), [](const auto& queued) {
                return queued.action == AndroidAutoTouchAction::Move;
            });
        if (move != pendingTouches_.end()) {
            pendingTouches_.erase(move);
            continue;
        }

        // Prefer dropping the oldest complete tap so Down/Up ordering of the
        // remaining gestures is preserved.
        bool removedTap = false;
        for (auto down = pendingTouches_.begin();
             down != pendingTouches_.end(); ++down) {
            const auto up = std::next(down);
            if (up == pendingTouches_.end()) break;
            if (down->action == AndroidAutoTouchAction::Down &&
                (up->action == AndroidAutoTouchAction::Up ||
                 up->action == AndroidAutoTouchAction::Cancel)) {
                pendingTouches_.erase(down, std::next(up));
                removedTap = true;
                break;
            }
        }
        if (!removedTap) break;
    }
    if (pendingTouches_.size() > kMaxPendingTouches + 2) {
        // Defensive recovery for a malformed event sequence. A Cancel is safer
        // than leaving Android Auto with a pointer permanently pressed.
        AndroidAutoTouchEvent cancel = event;
        cancel.action = AndroidAutoTouchAction::Cancel;
        pendingTouches_.clear();
        pendingTouches_.push_back(cancel);
    }
    return Result<void, AndroidAutoError>::success();
}

std::optional<AndroidAutoTouchEvent>
SwitchAndroidAutoHost::takePendingTouch() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pendingTouches_.empty()) return std::nullopt;
    auto event = pendingTouches_.front();
    pendingTouches_.pop_front();
    return event;
}

void SwitchAndroidAutoHost::setStatusHandler(StatusHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    statusHandler_ = std::move(handler);
}

void SwitchAndroidAutoHost::setVideoHandler(VideoHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    videoHandler_ = std::move(handler);
}

void SwitchAndroidAutoHost::setAudioHandler(AudioHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    audioHandler_ = std::move(handler);
}

void SwitchAndroidAutoHost::run() {
    const auto waitForStop = [this](auto delay) {
        std::unique_lock<std::mutex> lock(stopMutex_);
        return stopCondition_.wait_for(lock, delay,
            [this]() { return stopRequested_.load(); });
    };
    while (!stopRequested_) {
        const Endpoint endpoint =
            endpointProvider_ ? endpointProvider_() : Endpoint{};
        if (endpoint.address.empty() || endpoint.port == 0) {
            if (waitForStop(kReconnectDelay)) return;
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (session_.status().state == AndroidAutoHostState::Reconnecting ||
                session_.status().state == AndroidAutoHostState::Failed) {
                session_.reset();
                session_.startDiscovery();
            }
            const auto result = session_.beginConnection(
                endpoint.address + ":" + std::to_string(endpoint.port));
            if (!result) {
                session_.fail(result.error());
            }
        }
        publish();

        AndroidAutoError failure = AndroidAutoError::TransportUnavailable;
        const int socketFd = connectSocket(
            endpoint.address, endpoint.port, stopRequested_, failure);
        if (socketFd < 0) {
            if (!stopRequested_) {
                std::lock_guard<std::mutex> lock(mutex_);
                session_.transportDisconnected(failure);
            }
            publish();
            if (waitForStop(kReconnectDelay)) return;
            continue;
        }

        activeSocket_ = socketFd;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session_.transportConnected();
        }
        publish();

        const auto version = negotiateVersion(socketFd, stopRequested_);
        if (!version) {
            disconnectSocket();
            if (!stopRequested_) {
                std::lock_guard<std::mutex> lock(mutex_);
                session_.transportDisconnected(version.error());
            }
            publish();
            if (waitForStop(kReconnectDelay)) return;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto result = session_.versionNegotiated(version.value());
            if (!result) session_.fail(result.error());
            AndroidAutoCapabilities capabilities;
            capabilities.video = {
                AndroidAutoVideoCodec::H264,
                kProjectionWidth, kProjectionHeight, 60};
            capabilities.touch = true;
            // Required by Android Auto discovery. PCM is acknowledged and
            // discarded by the transport; the Switch never renders it.
            capabilities.mediaAudio = true;
            capabilities.navigationAudio = true;
            capabilities.systemAudio = true;
            capabilities.microphone = true;
            session_.negotiated(capabilities);
        }
        publish();

        const auto tlsResult = performTlsHandshake(socketFd, stopRequested_,
            [this]() { return takePendingTouch(); });
        if (tlsResult != AndroidAutoError::None) {
            disconnectSocket();
            if (!stopRequested_) {
                std::lock_guard<std::mutex> lock(mutex_);
                session_.transportDisconnected(tlsResult);
            }
            publish();
            if (waitForStop(kSecurityFailureDelay)) return;
            continue;
        }

        // TLS authentication is complete. Service discovery is the next stage;
        // until it is implemented, keep the authenticated connection alive.
        while (!stopRequested_) {
            pollfd descriptor{socketFd, POLLIN | POLLHUP | POLLERR, 0};
            const int result = ::poll(
                &descriptor, 1, static_cast<int>(kPollInterval.count()));
            if (result < 0 && errno != EINTR) break;
            if (result > 0 &&
                (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL))) break;
            if (result > 0 && (descriptor.revents & POLLIN)) {
                std::uint8_t probe = 0;
                const ssize_t received = ::recv(socketFd, &probe, 1, MSG_PEEK);
                if (received <= 0) break;
            }
        }
        disconnectSocket();
        if (!stopRequested_) {
            std::lock_guard<std::mutex> lock(mutex_);
            session_.transportDisconnected(AndroidAutoError::ConnectionClosed);
        }
        publish();
        if (waitForStop(kReconnectDelay)) return;
    }
}

void SwitchAndroidAutoHost::publish() {
    AndroidAutoHostStatus current;
    StatusHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current = session_.status();
        handler = statusHandler_;
    }
    if (!handler) return;
    events_.post([handler = std::move(handler), current]() {
        handler(current);
    });
}

void SwitchAndroidAutoHost::disconnectSocket() {
    const int socketFd = activeSocket_.exchange(-1);
    if (socketFd >= 0) {
        ::shutdown(socketFd, SHUT_RDWR);
        ::close(socketFd);
    }
}

}  // namespace switch_auto::platform::switch_platform
