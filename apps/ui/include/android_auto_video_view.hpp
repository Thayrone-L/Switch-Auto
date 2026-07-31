#pragma once

#include <borealis.hpp>

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

#include "switch_auto/android_auto.hpp"

namespace switch_auto::ui {

class AndroidAutoVideoView final : public brls::Image {
public:
    struct Yuv420Frame {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> data;
    };

    using TouchHandler =
        std::function<void(const AndroidAutoTouchEvent&)>;

    AndroidAutoVideoView();
    ~AndroidAutoVideoView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
        brls::Style style, brls::FrameContext* ctx) override;
    void dispatchTouch(AndroidAutoTouchAction action, brls::Point position);

    static void submitFrame(
        int width, int height, std::vector<std::uint8_t> rgba);
    static std::vector<std::uint8_t> acquireFrameBuffer(
        std::size_t requiredSize);
    static Yuv420Frame acquireYuv420Frame(int width, int height);
    static void submitYuv420Frame(Yuv420Frame frame);
    static bool canAcceptFrame();
    static void setTouchHandler(TouchHandler handler);
    static brls::View* create();

private:
    int videoTextures_[2] = {0, 0};
    int displayedTexture_ = 0;
    int videoWidth_ = 0;
    int videoHeight_ = 0;
    std::uint64_t displayedGeneration_ = 0;
    std::uint64_t uploadSamples_ = 0;
    std::uint64_t totalUploadMicros_ = 0;
    std::uint64_t maxUploadMicros_ = 0;
#ifdef __SWITCH__
    bool ensureYuvRenderer(NVGcontext* vg, int width, int height);
    bool updateYuvTexture(NVGcontext* vg, const Yuv420Frame& frame);
    unsigned int yuvPlaneTextures_[3] = {0, 0, 0};
    unsigned int yuvOutputTextures_[2] = {0, 0};
    unsigned int yuvFramebuffer_ = 0;
    unsigned int yuvProgram_ = 0;
    unsigned int yuvVertexArray_ = 0;
    int yuvNanoImages_[2] = {0, 0};
    int yuvDisplayedTexture_ = 0;
    int yuvWidth_ = 0;
    int yuvHeight_ = 0;
    bool displayingYuv_ = false;
#endif
};

}  // namespace switch_auto::ui
