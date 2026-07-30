#pragma once

#include <borealis.hpp>

#include <cstdint>
#include <functional>
#include <vector>

#include "switch_auto/android_auto.hpp"

namespace switch_auto::ui {

class AndroidAutoVideoView final : public brls::Image {
public:
    using TouchHandler =
        std::function<void(const AndroidAutoTouchEvent&)>;

    AndroidAutoVideoView();
    ~AndroidAutoVideoView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
        brls::Style style, brls::FrameContext* ctx) override;
    void dispatchTouch(AndroidAutoTouchAction action, brls::Point position);

    static void submitFrame(
        int width, int height, std::vector<std::uint8_t> rgba);
    static bool canAcceptFrame();
    static void setTouchHandler(TouchHandler handler);
    static brls::View* create();

private:
    int videoTexture_ = 0;
    int videoWidth_ = 0;
    int videoHeight_ = 0;
    std::uint64_t displayedGeneration_ = 0;
};

}  // namespace switch_auto::ui
