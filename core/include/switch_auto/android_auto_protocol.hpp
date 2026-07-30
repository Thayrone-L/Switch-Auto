#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "switch_auto/result.hpp"

namespace switch_auto {

enum class AndroidAutoProtocolError {
    InvalidHeader,
    InvalidFrameSize,
    FrameTooLarge,
    UnexpectedMessage,
    InvalidVersionResponse,
    VersionRejected,
    InvalidTlsHandshake,
};

enum class AndroidAutoFrameType : std::uint8_t {
    Middle = 0,
    First = 1,
    Last = 2,
    Bulk = 3,
};

struct AndroidAutoFrame {
    std::uint8_t channel = 0;
    AndroidAutoFrameType type = AndroidAutoFrameType::Bulk;
    bool encrypted = false;
    bool controlMessage = false;
    std::uint32_t totalSize = 0;
    std::vector<std::uint8_t> payload;
};

struct AndroidAutoProtocolVersion {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t status = 0;
};

class AndroidAutoFrameCodec {
public:
    static constexpr std::size_t kMaximumMessageSize = 1024 * 1024;

    static Result<std::vector<std::uint8_t>, AndroidAutoProtocolError> encode(
        const AndroidAutoFrame& frame);

    Result<std::vector<AndroidAutoFrame>, AndroidAutoProtocolError> feed(
        const std::uint8_t* data, std::size_t size);
    Result<std::vector<AndroidAutoFrame>, AndroidAutoProtocolError> feed(
        const std::vector<std::uint8_t>& data) {
        return feed(data.data(), data.size());
    }
    void reset();

private:
    std::vector<std::uint8_t> buffered_;
};

std::vector<std::uint8_t> makeAndroidAutoVersionRequest(
    std::uint16_t major = 1, std::uint16_t minor = 6);

Result<AndroidAutoProtocolVersion, AndroidAutoProtocolError>
parseAndroidAutoVersionResponse(const AndroidAutoFrame& frame);

std::vector<std::uint8_t> makeAndroidAutoTlsHandshake(
    const std::uint8_t* data, std::size_t size);
inline std::vector<std::uint8_t> makeAndroidAutoTlsHandshake(
    const std::vector<std::uint8_t>& data) {
    return makeAndroidAutoTlsHandshake(data.data(), data.size());
}

Result<std::vector<std::uint8_t>, AndroidAutoProtocolError>
parseAndroidAutoTlsHandshake(const AndroidAutoFrame& frame);

std::vector<std::uint8_t> makeAndroidAutoAuthComplete(
    std::int32_t status = 0);

}  // namespace switch_auto
