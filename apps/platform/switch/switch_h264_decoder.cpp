#include "switch_h264_decoder.hpp"

#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "android_auto_video_view.hpp"

namespace switch_auto::platform::switch_platform {

struct SwitchH264Decoder::Impl {
    const AVCodec* codec = nullptr;
    AVCodecContext* context = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* decoded = nullptr;
    SwsContext* converter = nullptr;
    int converterWidth = 0;
    int converterHeight = 0;
    AVPixelFormat converterFormat = AV_PIX_FMT_NONE;

    ~Impl() {
        sws_freeContext(converter);
        av_frame_free(&decoded);
        av_packet_free(&packet);
        avcodec_free_context(&context);
    }
};

SwitchH264Decoder::SwitchH264Decoder() : impl_(std::make_unique<Impl>()) {
    impl_->codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!impl_->codec) return;
    impl_->context = avcodec_alloc_context3(impl_->codec);
    impl_->packet = av_packet_alloc();
    impl_->decoded = av_frame_alloc();
    if (impl_->context) {
        impl_->context->flags |= AV_CODEC_FLAG_LOW_DELAY;
        impl_->context->thread_count = 2;
        impl_->context->thread_type = FF_THREAD_SLICE;
    }
    if (!impl_->context || !impl_->packet || !impl_->decoded ||
        avcodec_open2(impl_->context, impl_->codec, nullptr) < 0) {
        impl_.reset();
    }
}

SwitchH264Decoder::~SwitchH264Decoder() = default;

bool SwitchH264Decoder::available() const {
    return impl_ && impl_->context;
}

bool SwitchH264Decoder::decode(
    const std::uint8_t* data, std::size_t size) {
    if (!available() || !data || size == 0 ||
        size > static_cast<std::size_t>(INT_MAX)) {
        return false;
    }
    av_packet_unref(impl_->packet);
    impl_->packet->data = const_cast<std::uint8_t*>(data);
    impl_->packet->size = static_cast<int>(size);
    const int sendResult =
        avcodec_send_packet(impl_->context, impl_->packet);
    if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) return false;

    bool produced = false;
    while (avcodec_receive_frame(impl_->context, impl_->decoded) == 0) {
        const int width = impl_->decoded->width;
        const int height = impl_->decoded->height;
        const auto format =
            static_cast<AVPixelFormat>(impl_->decoded->format);
        if (!impl_->converter || width != impl_->converterWidth ||
            height != impl_->converterHeight ||
            format != impl_->converterFormat) {
            impl_->converter = sws_getCachedContext(impl_->converter,
                width, height, format, width, height, AV_PIX_FMT_RGBA,
                SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
            impl_->converterWidth = width;
            impl_->converterHeight = height;
            impl_->converterFormat = format;
        }
        if (!impl_->converter || width <= 0 || height <= 0) continue;

        // The decoder must consume every H.264 frame to preserve references,
        // but color conversion is wasted while the UI still has a frame
        // waiting for upload. Skip only the expensive YUV -> RGBA conversion.
        if (!switch_auto::ui::AndroidAutoVideoView::canAcceptFrame()) {
            produced = true;
            continue;
        }

        std::vector<std::uint8_t> rgba(
            static_cast<std::size_t>(width) * height * 4);
        std::uint8_t* outputData[]{rgba.data()};
        int outputStride[]{width * 4};
        sws_scale(impl_->converter, impl_->decoded->data,
            impl_->decoded->linesize, 0, height, outputData, outputStride);
        switch_auto::ui::AndroidAutoVideoView::submitFrame(
            width, height, std::move(rgba));
        produced = true;
    }
    return produced || sendResult >= 0;
}

}  // namespace switch_auto::platform::switch_platform
