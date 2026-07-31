#include "switch_auto_v2/video/video_surface_mailbox.hpp"

#include <mutex>
#include <condition_variable>
#include <deque>

namespace switch_auto::v2::video {
namespace {

std::mutex mailboxMutex;
constexpr std::size_t kSurfaceQueueCapacity = 2;
std::deque<std::unique_ptr<DecodedSurface>> surfaces;
SurfaceMailboxMetrics metrics;
std::condition_variable resetCompleted;
std::uint64_t resetGeneration = 0;
std::uint64_t completedResetGeneration = 0;
bool resetHandshakeDisabled = false;

}  // namespace

void publishDecodedSurface(std::unique_ptr<DecodedSurface> surface) {
    if (!surface) return;
    std::lock_guard<std::mutex> lock(mailboxMutex);
    ++metrics.published;
    if (surfaces.size() >= kSurfaceQueueCapacity) {
        surfaces.pop_front();
        ++metrics.replaced;
    }
    surfaces.push_back(std::move(surface));
    metrics.depth = surfaces.size();
    metrics.highWater = std::max<std::uint64_t>(
        metrics.highWater, metrics.depth);
}

std::unique_ptr<DecodedSurface> takeLatestDecodedSurface() {
    std::lock_guard<std::mutex> lock(mailboxMutex);
    if (surfaces.empty()) return {};
    auto surface = std::move(surfaces.front());
    surfaces.pop_front();
    ++metrics.taken;
    metrics.depth = surfaces.size();
    return surface;
}

SurfaceMailboxMetrics decodedSurfaceMailboxMetrics() {
    std::lock_guard<std::mutex> lock(mailboxMutex);
    return metrics;
}

void clearDecodedSurfaces() {
    std::lock_guard<std::mutex> lock(mailboxMutex);
    surfaces.clear();
    metrics.depth = 0;
}

bool requestDecodedSurfaceResetAndWait(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mailboxMutex);
    if (resetHandshakeDisabled) return true;
    surfaces.clear();
    metrics.depth = 0;
    const auto requestedGeneration = ++resetGeneration;
    return resetCompleted.wait_for(lock, timeout, [requestedGeneration]() {
        return resetHandshakeDisabled ||
            completedResetGeneration >= requestedGeneration;
    });
}

bool decodedSurfaceResetRequested() {
    std::lock_guard<std::mutex> lock(mailboxMutex);
    return completedResetGeneration < resetGeneration;
}

void completeDecodedSurfaceReset() {
    {
        std::lock_guard<std::mutex> lock(mailboxMutex);
        surfaces.clear();
        metrics.depth = 0;
        completedResetGeneration = resetGeneration;
    }
    resetCompleted.notify_all();
}

void disableDecodedSurfaceResetHandshake() {
    {
        std::lock_guard<std::mutex> lock(mailboxMutex);
        resetHandshakeDisabled = true;
        surfaces.clear();
        metrics.depth = 0;
    }
    resetCompleted.notify_all();
}

}  // namespace switch_auto::v2::video
