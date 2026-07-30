#include "android_auto_video_view.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>

namespace switch_auto::ui {
namespace {

std::mutex frameMutex;
std::vector<std::uint8_t> pendingFrame;
int pendingWidth = 0;
int pendingHeight = 0;
std::uint64_t pendingGeneration = 0;
AndroidAutoVideoView::TouchHandler touchHandler;

}  // namespace

AndroidAutoVideoView::AndroidAutoVideoView() {
}

AndroidAutoVideoView::~AndroidAutoVideoView() = default;

void AndroidAutoVideoView::dispatchTouch(
    AndroidAutoTouchAction action, brls::Point position) {
    const brls::Rect frame = getFrame();
    if (frame.size.width <= 0 || frame.size.height <= 0) return;

    AndroidAutoTouchEvent event;
    event.action = action;
    event.normalizedX = std::clamp(
        (position.x - frame.origin.x) / frame.size.width, 0.0f, 1.0f);
    event.normalizedY = std::clamp(
        (position.y - frame.origin.y) / frame.size.height, 0.0f, 1.0f);
    event.timestampMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch())
            .count());

    TouchHandler handler;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        handler = touchHandler;
    }
    if (handler) handler(event);
}

void AndroidAutoVideoView::submitFrame(
    int width, int height, std::vector<std::uint8_t> rgba) {
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<std::size_t>(width) * height * 4) {
        return;
    }
    std::lock_guard<std::mutex> lock(frameMutex);
    pendingWidth = width;
    pendingHeight = height;
    pendingFrame = std::move(rgba);
    ++pendingGeneration;
}

bool AndroidAutoVideoView::canAcceptFrame() {
    std::lock_guard<std::mutex> lock(frameMutex);
    return pendingFrame.empty();
}

void AndroidAutoVideoView::setTouchHandler(TouchHandler handler) {
    std::lock_guard<std::mutex> lock(frameMutex);
    touchHandler = std::move(handler);
}

void AndroidAutoVideoView::draw(NVGcontext* vg, float x, float y,
    float width, float height, brls::Style style,
    brls::FrameContext* ctx) {
    std::vector<std::uint8_t> nextFrame;
    int nextWidth = 0;
    int nextHeight = 0;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (pendingGeneration != displayedGeneration_) {
            // Transfer ownership to the render thread. Copying a 1280x720 RGBA
            // frame here cost roughly 3.5 MiB on every displayed frame.
            nextFrame = std::move(pendingFrame);
            nextWidth = pendingWidth;
            nextHeight = pendingHeight;
            displayedGeneration_ = pendingGeneration;
        }
    }

    if (!nextFrame.empty()) {
        if (!videoTexture_ || nextWidth != videoWidth_ ||
            nextHeight != videoHeight_) {
            if (videoTexture_) nvgDeleteImage(vg, videoTexture_);
            videoTexture_ = nvgCreateImageRGBA(vg, nextWidth, nextHeight,
                0, nextFrame.data());
            videoWidth_ = nextWidth;
            videoHeight_ = nextHeight;
        } else {
            nvgUpdateImage(vg, videoTexture_, nextFrame.data());
        }
    }

    if (!videoTexture_) {
        brls::Image::draw(vg, x, y, width, height, style, ctx);
        return;
    }
    const NVGpaint image = nvgImagePattern(
        vg, x, y, width, height, 0, videoTexture_, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillPaint(vg, image);
    nvgFill(vg);

}

brls::View* AndroidAutoVideoView::create() {
    return new AndroidAutoVideoView();
}

}  // namespace switch_auto::ui
