#pragma once

#include <memory>
#include <chrono>

#include "switch_auto_v2/video/video_decoder.hpp"

namespace switch_auto::v2::video {

struct SurfaceMailboxMetrics {
    std::uint64_t published = 0;
    std::uint64_t taken = 0;
    std::uint64_t replaced = 0;
    std::uint64_t depth = 0;
    std::uint64_t highWater = 0;
};

void publishDecodedSurface(std::unique_ptr<DecodedSurface> surface);
std::unique_ptr<DecodedSurface> takeLatestDecodedSurface();
SurfaceMailboxMetrics decodedSurfaceMailboxMetrics();
void clearDecodedSurfaces();
bool requestDecodedSurfaceResetAndWait(std::chrono::milliseconds timeout);
bool decodedSurfaceResetRequested();
void completeDecodedSurfaceReset();
void disableDecodedSurfaceResetHandshake();

}  // namespace switch_auto::v2::video
