#include "switch_auto_v2/platform/nvtegra_h264_decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

#include <array>
#include <climits>
#include <string>

#include "switch_auto_v2/platform/nvtegra_probe.hpp"

namespace switch_auto::v2::platform {

namespace {

std::string errorText(int error) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(error, text.data(), text.size());
    return text.data();
}

}  // namespace

struct NvTegraH264Decoder::Impl {
    AVCodecContext* context = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* receiveFrame = nullptr;
    std::string error;

    ~Impl() {
        av_frame_free(&receiveFrame);
        av_packet_free(&packet);
        avcodec_free_context(&context);
    }
};

NvTegraDecodedSurface::NvTegraDecodedSurface(AVFrame* frame) : frame_(frame) {}

NvTegraDecodedSurface::~NvTegraDecodedSurface() {
    av_frame_free(&frame_);
}

AVFrame* NvTegraDecodedSurface::frame() const {
    return frame_;
}

NvTegraH264Decoder::NvTegraH264Decoder() : impl_(std::make_unique<Impl>()) {}

NvTegraH264Decoder::~NvTegraH264Decoder() = default;

bool NvTegraH264Decoder::initialize(
    int width, int height, int framesPerSecond) {
    if (!impl_ || width <= 0 || height <= 0 || framesPerSecond <= 0) {
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        impl_->error = "H.264 decoder unavailable";
        return false;
    }
    impl_->context = avcodec_alloc_context3(codec);
    impl_->packet = av_packet_alloc();
    impl_->receiveFrame = av_frame_alloc();
    if (!impl_->context || !impl_->packet || !impl_->receiveFrame) {
        impl_->error = "FFmpeg allocation failed";
        return false;
    }

    impl_->context->width = width;
    impl_->context->height = height;
    impl_->context->framerate = AVRational{framesPerSecond, 1};
    impl_->context->flags |= AV_CODEC_FLAG_LOW_DELAY;
    impl_->context->thread_count = 1;
    impl_->context->pix_fmt = AV_PIX_FMT_NVTEGRA;
    impl_->context->hw_device_ctx = acquireNvTegraDevice();
    if (!impl_->context->hw_device_ctx) {
        impl_->error = "NVTEGRA device is not initialized";
        return false;
    }

    const int openResult = avcodec_open2(impl_->context, codec, nullptr);
    if (openResult < 0) {
        impl_->error = "H.264 NVTEGRA open failed: " + errorText(openResult);
        return false;
    }
    return true;
}

video::DecodeStatus NvTegraH264Decoder::submit(
    const video::EncodedAccessUnit& accessUnit) {
    if (!impl_ || !impl_->context || !accessUnit.data ||
        accessUnit.size == 0 || accessUnit.size > INT_MAX) {
        return video::DecodeStatus::fatalError;
    }
    av_packet_unref(impl_->packet);
    impl_->packet->data = const_cast<std::uint8_t*>(accessUnit.data);
    impl_->packet->size = static_cast<int>(accessUnit.size);
    impl_->packet->pts = static_cast<std::int64_t>(accessUnit.presentationTimeUs);
    const int result = avcodec_send_packet(impl_->context, impl_->packet);
    if (result == AVERROR(EAGAIN)) return video::DecodeStatus::needsDrain;
    if (result < 0) {
        impl_->error = "H.264 packet rejected: " + errorText(result);
        return video::DecodeStatus::needsKeyframe;
    }
    return video::DecodeStatus::accepted;
}

std::unique_ptr<video::DecodedSurface> NvTegraH264Decoder::receive() {
    if (!impl_ || !impl_->context) return nullptr;
    av_frame_unref(impl_->receiveFrame);
    const int result = avcodec_receive_frame(
        impl_->context, impl_->receiveFrame);
    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return nullptr;
    if (result < 0) {
        impl_->error = "H.264 frame receive failed: " + errorText(result);
        return nullptr;
    }
    if (impl_->receiveFrame->format != AV_PIX_FMT_NVTEGRA) {
        impl_->error = "Decoder returned a non-NVTEGRA frame";
        return nullptr;
    }
    AVFrame* owned = av_frame_clone(impl_->receiveFrame);
    return owned ? std::make_unique<NvTegraDecodedSurface>(owned) : nullptr;
}

void NvTegraH264Decoder::flush() {
    if (impl_ && impl_->context) avcodec_flush_buffers(impl_->context);
}

const char* NvTegraH264Decoder::lastError() const {
    return impl_ ? impl_->error.c_str() : "decoder unavailable";
}

}  // namespace switch_auto::v2::platform

