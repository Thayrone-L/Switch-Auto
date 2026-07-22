#include <ctime>

#include <switch.h>

#include "switch_auto/application.hpp"

namespace {

void updateRuntimeState(switch_auto::MutableRuntimeService& runtime) {
    auto& state = runtime.mutableState();

    const std::time_t timestamp = std::time(nullptr);
    std::tm localTime{};
    localtime_r(&timestamp, &localTime);
    state.hour = localTime.tm_hour;
    state.minute = localTime.tm_min;

    u32 battery = 0;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&battery))) {
        state.batteryPercent = static_cast<int>(battery);
    }

    state.speedKph = 0;
    state.companionConnected = false;
    state.track = "NO MEDIA";
    state.artist = "ANDROID COMPANION OFFLINE";
}
}  // namespace

int main(int, char**) {
    switch_auto::MutableRuntimeService runtime;
    switch_auto::Application application(runtime);
    if (!application.initialize()) return 1;

    psmInitialize();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    Framebuffer framebuffer;
    const Result framebufferResult = framebufferCreate(
        &framebuffer, nwindowGetDefault(), switch_auto::kLogicalWidth,
        switch_auto::kLogicalHeight, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(framebufferResult)) {
        application.shutdown();
        psmExit();
        return 2;
    }
    framebufferMakeLinear(&framebuffer);

    int frame = 0;
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;

        if (frame == 0) updateRuntimeState(runtime);
        frame = (frame + 1) % 60;

        u32 strideBytes = 0;
        auto* pixels = static_cast<u32*>(framebufferBegin(&framebuffer, &strideBytes));
        switch_auto::Canvas canvas(
            pixels, switch_auto::kLogicalWidth, switch_auto::kLogicalHeight,
            static_cast<int>(strideBytes / sizeof(u32)));
        application.update(1.0f / 60.0f);
        application.render(canvas);
        framebufferEnd(&framebuffer);
    }

    application.shutdown();
    framebufferClose(&framebuffer);
    psmExit();
    return 0;
}
