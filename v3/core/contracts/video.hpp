#pragma once

#include "core/base/byte_span.hpp"
#include "core/base/cancellation.hpp"
#include "core/base/result.hpp"

#include <cstdint>

namespace switch_auto::contracts {

enum class VideoErrorCode : std::uint8_t {
    invalid_data,
    unsupported_format,
    resource_exhausted,
    decoder_failure,
    invalid_surface,
    timed_out,
    cancelled,
};

struct VideoError {
    VideoErrorCode code = VideoErrorCode::decoder_failure;
    std::int32_t implementation_code = 0;
};

struct EncodedAccessUnit {
    base::ConstByteSpan bytes{};
    std::uint64_t presentation_time_us = 0;
    bool codec_configuration = false;
    bool key_frame = false;
};

enum class PixelFormat : std::uint8_t {
    nv12,
};

struct DecodedSurface {
    DecodedSurface() noexcept = default;
    DecodedSurface(std::uint64_t surface_handle,
                   std::uint32_t surface_width,
                   std::uint32_t surface_height,
                   std::uint32_t surface_stride,
                   PixelFormat surface_format,
                   std::uint64_t pts_us) noexcept
        : handle(surface_handle),
          width(surface_width),
          height(surface_height),
          stride(surface_stride),
          format(surface_format),
          presentation_time_us(pts_us) {}

    DecodedSurface(const DecodedSurface&) = delete;
    DecodedSurface& operator=(const DecodedSurface&) = delete;
    DecodedSurface(DecodedSurface&& other) noexcept
        : handle(other.handle),
          width(other.width),
          height(other.height),
          stride(other.stride),
          format(other.format),
          presentation_time_us(other.presentation_time_us) {
        other.handle = 0;
    }
    DecodedSurface& operator=(DecodedSurface&& other) noexcept {
        if (this != &other) {
            handle = other.handle;
            width = other.width;
            height = other.height;
            stride = other.stride;
            format = other.format;
            presentation_time_us = other.presentation_time_us;
            other.handle = 0;
        }
        return *this;
    }

    std::uint64_t handle = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    PixelFormat format = PixelFormat::nv12;
    std::uint64_t presentation_time_us = 0;
};

// Decoder methods are thread-affine to one decode worker. decode() borrows the
// access-unit bytes only for the call and returns ownership of one opaque surface
// lease. A successful surface has a non-zero handle and must be returned exactly
// once through release() on the decoder thread. The lease is move-only.
class IVideoDecoder {
public:
    virtual ~IVideoDecoder() = default;

    [[nodiscard]] virtual base::Result<DecodedSurface, VideoError> decode(
        const EncodedAccessUnit& access_unit,
        base::Deadline deadline,
        base::CancellationToken cancellation) noexcept = 0;

    virtual void release(DecodedSurface&& surface) noexcept = 0;
};

// Presenter methods are thread-affine to the render/UI thread. present() borrows
// a valid surface lease for the duration of the call and must not retain or
// release it. A mailbox transfers the lease between threads; the decoder owner
// ultimately performs release(). No call waits for network or protocol work.
class IVideoPresenter {
public:
    virtual ~IVideoPresenter() = default;

    [[nodiscard]] virtual base::Result<void, VideoError> present(
        const DecodedSurface& surface,
        base::Deadline deadline,
        base::CancellationToken cancellation) noexcept = 0;
};

}  // namespace switch_auto::contracts
