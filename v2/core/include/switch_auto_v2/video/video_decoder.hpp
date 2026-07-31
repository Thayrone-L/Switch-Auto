#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace switch_auto::v2::video {

struct EncodedAccessUnit {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::uint64_t presentationTimeUs = 0;
};

class DecodedSurface {
public:
    virtual ~DecodedSurface() = default;
};

enum class DecodeStatus {
    accepted,
    needsDrain,
    needsKeyframe,
    fatalError,
};

class IVideoDecoder {
public:
    virtual ~IVideoDecoder() = default;

    virtual bool initialize(int width, int height, int framesPerSecond) = 0;
    virtual DecodeStatus submit(const EncodedAccessUnit& accessUnit) = 0;
    virtual std::unique_ptr<DecodedSurface> receive() = 0;
    virtual void flush() = 0;
};

}  // namespace switch_auto::v2::video

