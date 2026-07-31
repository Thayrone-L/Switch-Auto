#pragma once

#include <memory>

#include "switch_auto_v2/video/video_decoder.hpp"

extern "C" {
struct AVFrame;
}

namespace switch_auto::v2::platform {

class NvTegraDecodedSurface final : public video::DecodedSurface {
public:
    explicit NvTegraDecodedSurface(AVFrame* frame);
    ~NvTegraDecodedSurface() override;

    NvTegraDecodedSurface(const NvTegraDecodedSurface&) = delete;
    NvTegraDecodedSurface& operator=(const NvTegraDecodedSurface&) = delete;

    AVFrame* frame() const;

private:
    AVFrame* frame_ = nullptr;
};

class NvTegraH264Decoder final : public video::IVideoDecoder {
public:
    NvTegraH264Decoder();
    ~NvTegraH264Decoder() override;

    bool initialize(int width, int height, int framesPerSecond) override;
    video::DecodeStatus submit(
        const video::EncodedAccessUnit& accessUnit) override;
    std::unique_ptr<video::DecodedSurface> receive() override;
    void flush() override;

    const char* lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace switch_auto::v2::platform

