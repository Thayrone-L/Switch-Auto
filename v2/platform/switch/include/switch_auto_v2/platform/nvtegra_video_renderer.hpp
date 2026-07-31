#pragma once

#include <memory>

#include "switch_auto_v2/video/video_decoder.hpp"

namespace switch_auto::v2::platform {

struct VideoRendererMetrics {
    std::uint64_t presented = 0;
    std::uint64_t totalAcquireMicros = 0;
    std::uint64_t maximumAcquireMicros = 0;
};

class NvTegraVideoRenderer {
public:
    NvTegraVideoRenderer();
    ~NvTegraVideoRenderer();

    bool initialize(int width, int height);
    bool present(std::unique_ptr<video::DecodedSurface> surface);
    void renderIdle();
    VideoRendererMetrics metrics() const;
    void resetDecoderSurfaces();
    void shutdown();
    const char* lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace switch_auto::v2::platform
