#include "switch_auto/android_auto_protocol.hpp"

#include <algorithm>
#include <limits>

namespace switch_auto {
namespace {

constexpr std::uint8_t kFrameTypeMask = 0x03;
constexpr std::uint8_t kControlMessageFlag = 0x04;
constexpr std::uint8_t kEncryptedFlag = 0x08;
constexpr std::uint8_t kKnownFlags = 0x0f;
constexpr std::uint16_t kVersionRequestMessage = 0x0001;
constexpr std::uint16_t kVersionResponseMessage = 0x0002;
constexpr std::uint16_t kEncapsulatedSslMessage = 0x0003;
constexpr std::uint16_t kAuthCompleteMessage = 0x0004;

std::uint16_t read16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
}

std::uint32_t read32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
        (static_cast<std::uint32_t>(bytes[1]) << 16) |
        (static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
}

void append16(std::vector<std::uint8_t>& target, std::uint16_t value) {
    target.push_back(static_cast<std::uint8_t>(value >> 8));
    target.push_back(static_cast<std::uint8_t>(value));
}

void append32(std::vector<std::uint8_t>& target, std::uint32_t value) {
    target.push_back(static_cast<std::uint8_t>(value >> 24));
    target.push_back(static_cast<std::uint8_t>(value >> 16));
    target.push_back(static_cast<std::uint8_t>(value >> 8));
    target.push_back(static_cast<std::uint8_t>(value));
}

std::vector<std::uint8_t> encodeControlMessage(
    std::uint16_t messageId, const std::uint8_t* data, std::size_t size) {
    if (size > AndroidAutoFrameCodec::kMaximumMessageSize - 2) return {};
    AndroidAutoFrame frame;
    frame.channel = 0;
    frame.type = AndroidAutoFrameType::Bulk;
    frame.payload.reserve(size + 2);
    append16(frame.payload, messageId);
    if (data != nullptr && size > 0) {
        frame.payload.insert(frame.payload.end(), data, data + size);
    }
    auto encoded = AndroidAutoFrameCodec::encode(frame);
    return encoded ? std::move(encoded.value()) : std::vector<std::uint8_t>{};
}

}  // namespace

Result<std::vector<std::uint8_t>, AndroidAutoProtocolError>
AndroidAutoFrameCodec::encode(const AndroidAutoFrame& frame) {
    if (frame.payload.size() > std::numeric_limits<std::uint16_t>::max() ||
        frame.payload.size() > kMaximumMessageSize) {
        return Result<std::vector<std::uint8_t>,
            AndroidAutoProtocolError>::failure(
            AndroidAutoProtocolError::FrameTooLarge);
    }
    if (frame.type == AndroidAutoFrameType::First &&
        (frame.totalSize < frame.payload.size() ||
            frame.totalSize > kMaximumMessageSize)) {
        return Result<std::vector<std::uint8_t>,
            AndroidAutoProtocolError>::failure(
            AndroidAutoProtocolError::InvalidFrameSize);
    }

    std::vector<std::uint8_t> data;
    data.reserve(4 + (frame.type == AndroidAutoFrameType::First ? 4 : 0) +
        frame.payload.size());
    data.push_back(frame.channel);
    data.push_back(static_cast<std::uint8_t>(frame.type) |
        (frame.controlMessage ? kControlMessageFlag : 0) |
        (frame.encrypted ? kEncryptedFlag : 0));
    append16(data, static_cast<std::uint16_t>(frame.payload.size()));
    if (frame.type == AndroidAutoFrameType::First) {
        append32(data, frame.totalSize);
    }
    data.insert(data.end(), frame.payload.begin(), frame.payload.end());
    return Result<std::vector<std::uint8_t>,
        AndroidAutoProtocolError>::success(std::move(data));
}

Result<std::vector<AndroidAutoFrame>, AndroidAutoProtocolError>
AndroidAutoFrameCodec::feed(const std::uint8_t* data, std::size_t size) {
    if (data != nullptr && size > 0) {
        buffered_.insert(buffered_.end(), data, data + size);
    }

    std::vector<AndroidAutoFrame> frames;
    std::size_t offset = 0;
    while (buffered_.size() - offset >= 4) {
        const auto* header = buffered_.data() + offset;
        if ((header[1] & ~kKnownFlags) != 0) {
            reset();
            return Result<std::vector<AndroidAutoFrame>,
                AndroidAutoProtocolError>::failure(
                AndroidAutoProtocolError::InvalidHeader);
        }

        const auto type =
            static_cast<AndroidAutoFrameType>(header[1] & kFrameTypeMask);
        const std::size_t headerSize =
            type == AndroidAutoFrameType::First ? 8 : 4;
        if (buffered_.size() - offset < headerSize) break;

        const std::size_t payloadSize = read16(header + 2);
        std::uint32_t totalSize = static_cast<std::uint32_t>(payloadSize);
        if (type == AndroidAutoFrameType::First) {
            totalSize = read32(header + 4);
            if (totalSize < payloadSize || totalSize > kMaximumMessageSize) {
                reset();
                return Result<std::vector<AndroidAutoFrame>,
                    AndroidAutoProtocolError>::failure(
                    AndroidAutoProtocolError::InvalidFrameSize);
            }
        }
        if (payloadSize > kMaximumMessageSize) {
            reset();
            return Result<std::vector<AndroidAutoFrame>,
                AndroidAutoProtocolError>::failure(
                AndroidAutoProtocolError::FrameTooLarge);
        }
        if (buffered_.size() - offset < headerSize + payloadSize) break;

        AndroidAutoFrame frame;
        frame.channel = header[0];
        frame.type = type;
        frame.controlMessage = (header[1] & kControlMessageFlag) != 0;
        frame.encrypted = (header[1] & kEncryptedFlag) != 0;
        frame.totalSize = totalSize;
        frame.payload.assign(header + headerSize,
            header + headerSize + payloadSize);
        frames.push_back(std::move(frame));
        offset += headerSize + payloadSize;
    }

    if (offset > 0) {
        buffered_.erase(buffered_.begin(), buffered_.begin() + offset);
    }
    if (buffered_.size() > kMaximumMessageSize + 8) {
        reset();
        return Result<std::vector<AndroidAutoFrame>,
            AndroidAutoProtocolError>::failure(
            AndroidAutoProtocolError::FrameTooLarge);
    }
    return Result<std::vector<AndroidAutoFrame>,
        AndroidAutoProtocolError>::success(std::move(frames));
}

void AndroidAutoFrameCodec::reset() {
    buffered_.clear();
}

std::vector<std::uint8_t> makeAndroidAutoVersionRequest(
    std::uint16_t major, std::uint16_t minor) {
    std::vector<std::uint8_t> payload;
    append16(payload, major);
    append16(payload, minor);
    return encodeControlMessage(
        kVersionRequestMessage, payload.data(), payload.size());
}

Result<AndroidAutoProtocolVersion, AndroidAutoProtocolError>
parseAndroidAutoVersionResponse(const AndroidAutoFrame& frame) {
    if (frame.channel != 0 || frame.type != AndroidAutoFrameType::Bulk ||
        frame.encrypted || frame.payload.size() != 8 ||
        read16(frame.payload.data()) != kVersionResponseMessage) {
        return Result<AndroidAutoProtocolVersion,
            AndroidAutoProtocolError>::failure(
            AndroidAutoProtocolError::InvalidVersionResponse);
    }

    AndroidAutoProtocolVersion version;
    version.major = read16(frame.payload.data() + 2);
    version.minor = read16(frame.payload.data() + 4);
    version.status = read16(frame.payload.data() + 6);
    if (version.status != 0) {
        return Result<AndroidAutoProtocolVersion,
            AndroidAutoProtocolError>::failure(
            AndroidAutoProtocolError::VersionRejected);
    }
    return Result<AndroidAutoProtocolVersion,
        AndroidAutoProtocolError>::success(version);
}

std::vector<std::uint8_t> makeAndroidAutoTlsHandshake(
    const std::uint8_t* data, std::size_t size) {
    return encodeControlMessage(kEncapsulatedSslMessage, data, size);
}

Result<std::vector<std::uint8_t>, AndroidAutoProtocolError>
parseAndroidAutoTlsHandshake(const AndroidAutoFrame& frame) {
    if (frame.channel != 0 || frame.type != AndroidAutoFrameType::Bulk ||
        frame.encrypted || frame.payload.size() < 2 ||
        read16(frame.payload.data()) != kEncapsulatedSslMessage) {
        return Result<std::vector<std::uint8_t>,
            AndroidAutoProtocolError>::failure(
            AndroidAutoProtocolError::InvalidTlsHandshake);
    }
    return Result<std::vector<std::uint8_t>,
        AndroidAutoProtocolError>::success(
        std::vector<std::uint8_t>(
            frame.payload.begin() + 2, frame.payload.end()));
}

std::vector<std::uint8_t> makeAndroidAutoAuthComplete(std::int32_t status) {
    // AuthResponse is proto2: required int32 status = 1.
    // The only currently valid status is success (0), encoded as tag 0x08,
    // value 0x00. Keep non-zero support limited to the one-byte varint range.
    if (status < 0 || status > 0x7f) return {};
    const std::uint8_t payload[]{
        0x08, static_cast<std::uint8_t>(status)};
    return encodeControlMessage(
        kAuthCompleteMessage, payload, sizeof(payload));
}

}  // namespace switch_auto
