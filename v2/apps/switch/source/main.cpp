#include <switch.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

#include "switch_android_auto_host.hpp"
#include "switch_auto/event_queue.hpp"
#include "switch_auto_v2/platform/nvtegra_h264_decoder.hpp"
#include "switch_auto_v2/platform/nvtegra_probe.hpp"
#include "switch_auto_v2/platform/nvtegra_video_renderer.hpp"
#include "switch_auto_v2/video/video_surface_mailbox.hpp"

namespace {

constexpr unsigned kWidth = 1280;
constexpr unsigned kHeight = 720;

void logStage(std::FILE* log, const char* stage) {
    if (!log) return;
    std::fprintf(log, "%s\n", stage);
    std::fflush(log);
}

}  // namespace

int main() {
    std::FILE* log = std::fopen(
        "sdmc:/switch/switch-auto-v2/v2.log", "w");
    logStage(log, "startup: entered Deko3D probe");

    const auto nvtegra = switch_auto::v2::platform::probeNvTegra();
    logStage(log, nvtegra.available
        ? "startup: NVTEGRA ready"
        : "startup: NVTEGRA unavailable");

    auto decoder = std::make_unique<
        switch_auto::v2::platform::NvTegraH264Decoder>();
    const bool decoderReady = nvtegra.available &&
        decoder->initialize(kWidth, kHeight, 60);
    logStage(log, decoderReady
        ? "startup: H.264 NVTEGRA decoder ready"
        : decoder->lastError());
    decoder.reset();

    switch_auto::v2::platform::NvTegraVideoRenderer renderer;
    if (!renderer.initialize(kWidth, kHeight)) {
        logStage(log, "startup: Deko3D initialization failed");
        logStage(log, renderer.lastError());
        if (log) std::fclose(log);
        return 1;
    }
    logStage(log, "startup: Deko3D ready");

    switch_auto::EventQueue events;
    using AndroidAutoHost =
        switch_auto::platform::switch_platform::SwitchAndroidAutoHost;
    auto androidAuto = std::make_unique<AndroidAutoHost>(events, []() {
        return AndroidAutoHost::Endpoint{"192.168.15.5", 5288};
    });
    androidAuto->setStatusHandler([log](const auto& status) {
        if (!log) return;
        std::fprintf(log, "android-auto: state=%d error=%d endpoint=%s\n",
            static_cast<int>(status.state),
            static_cast<int>(status.lastError),
            status.endpointAddress.c_str());
        std::fflush(log);
    });
    const auto hostStart = androidAuto->start();
    logStage(log, hostStart
        ? "android-auto: host started"
        : "android-auto: host start failed");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    hidInitializeTouchScreen();
    PadState pad;
    padInitializeDefault(&pad);
    bool touchActive = false;
    std::uint32_t activeFinger = 0;
    std::uint32_t lastTouchX = 0;
    std::uint32_t lastTouchY = 0;
    std::uint64_t lastTouchSample = 0;
    const auto sendTouch = [&androidAuto](
        switch_auto::AndroidAutoTouchAction action,
        std::uint32_t x, std::uint32_t y) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        switch_auto::AndroidAutoTouchEvent event;
        event.action = action;
        event.normalizedX = std::min(1.0f,
            static_cast<float>(x) / static_cast<float>(kWidth - 1));
        event.normalizedY = std::min(1.0f,
            static_cast<float>(y) / static_cast<float>(kHeight - 1));
        event.timestampMicros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now).count());
        androidAuto->sendTouch(event);
    };
    auto previousMetricsAt = std::chrono::steady_clock::now();
    std::uint64_t previousPublished = 0;
    std::uint64_t previousPresented = 0;
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        events.drain();

        if (switch_auto::v2::video::decodedSurfaceResetRequested()) {
            renderer.resetDecoderSurfaces();
            switch_auto::v2::video::completeDecodedSurfaceReset();
            logStage(log, "pipeline: decoder surface generation reset");
        }

        HidTouchScreenState touchState{};
        const bool hasState =
            hidGetTouchScreenStates(&touchState, 1) != 0;
        if (hasState && touchState.count > 0) {
            const auto& touch = touchState.touches[0];
            if (!touchActive || touch.finger_id != activeFinger) {
                if (touchActive) {
                    sendTouch(switch_auto::AndroidAutoTouchAction::Cancel,
                        lastTouchX, lastTouchY);
                }
                touchActive = true;
                activeFinger = touch.finger_id;
                lastTouchX = touch.x;
                lastTouchY = touch.y;
                lastTouchSample = touchState.sampling_number;
                sendTouch(switch_auto::AndroidAutoTouchAction::Down,
                    touch.x, touch.y);
            } else if (touchState.sampling_number != lastTouchSample &&
                (touch.x != lastTouchX || touch.y != lastTouchY)) {
                lastTouchX = touch.x;
                lastTouchY = touch.y;
                lastTouchSample = touchState.sampling_number;
                sendTouch(switch_auto::AndroidAutoTouchAction::Move,
                    touch.x, touch.y);
            }
        } else if (touchActive) {
            sendTouch(switch_auto::AndroidAutoTouchAction::Up,
                lastTouchX, lastTouchY);
            touchActive = false;
        }

        auto surface =
            switch_auto::v2::video::takeLatestDecodedSurface();
        if (surface) {
            if (!renderer.present(std::move(surface))) {
                logStage(log, renderer.lastError());
            }
        } else {
            renderer.renderIdle();
        }

        const auto metricsNow = std::chrono::steady_clock::now();
        if (metricsNow - previousMetricsAt >= std::chrono::seconds(2)) {
            const auto mailbox =
                switch_auto::v2::video::decodedSurfaceMailboxMetrics();
            const auto graphics = renderer.metrics();
            const double windowSeconds = std::chrono::duration<double>(
                metricsNow - previousMetricsAt).count();
            const double decodedFps =
                (mailbox.published - previousPublished) / windowSeconds;
            const double presentedFps =
                (graphics.presented - previousPresented) / windowSeconds;
            const double acquireAverageMs = graphics.presented > 0
                ? static_cast<double>(graphics.totalAcquireMicros) /
                    static_cast<double>(graphics.presented) / 1000.0
                : 0.0;
            if (log) {
                std::fprintf(log,
                    "pipeline: decoded_fps=%.2f presented_fps=%.2f "
                    "published=%llu presented=%llu replaced=%llu "
                    "queue_depth=%llu queue_high=%llu "
                    "acquire_avg_ms=%.3f acquire_max_ms=%.3f\n",
                    decodedFps, presentedFps,
                    static_cast<unsigned long long>(mailbox.published),
                    static_cast<unsigned long long>(graphics.presented),
                    static_cast<unsigned long long>(mailbox.replaced),
                    static_cast<unsigned long long>(mailbox.depth),
                    static_cast<unsigned long long>(mailbox.highWater),
                    acquireAverageMs,
                    static_cast<double>(graphics.maximumAcquireMicros) /
                        1000.0);
                std::fflush(log);
            }
            previousMetricsAt = metricsNow;
            previousPublished = mailbox.published;
            previousPresented = graphics.presented;
        }
    }

    logStage(log, "shutdown: requested");
    // Imported Deko3D memory blocks refer to NVTEGRA decoder surfaces. Drain
    // and release the GPU imports while their backing decoder pool still
    // exists; reversing this order can panic inside the graphics driver.
    switch_auto::v2::video::disableDecodedSurfaceResetHandshake();
    renderer.shutdown();
    logStage(log, "shutdown: Deko3D released");
    androidAuto->stop();
    events.drain();
    androidAuto.reset();
    switch_auto::v2::video::clearDecodedSurfaces();
    logStage(log, "shutdown: Android Auto host released");
    switch_auto::v2::platform::shutdownNvTegra();
    logStage(log, "shutdown: NVTEGRA released");
    if (log) std::fclose(log);
    return 0;
}
