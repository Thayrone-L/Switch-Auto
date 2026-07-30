#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "switch_auto/android_auto.hpp"
#include "switch_auto/event_queue.hpp"

namespace switch_auto::platform::switch_platform {

class SwitchAndroidAutoHost final : public AndroidAutoHostService {
public:
    struct Endpoint {
        std::string address;
        std::uint16_t port = 0;
    };
    using EndpointProvider = std::function<Endpoint()>;

    SwitchAndroidAutoHost(EventQueue& events, EndpointProvider endpointProvider);
    ~SwitchAndroidAutoHost() override;

    Result<void, AndroidAutoError> start() override;
    void update() override {}
    void suspend() override;
    void resume() override;
    void stop() override;
    AndroidAutoHostStatus status() const override;
    Result<void, AndroidAutoError> sendTouch(
        const AndroidAutoTouchEvent& event) override;
    void setStatusHandler(StatusHandler handler) override;
    void setVideoHandler(VideoHandler handler) override;
    void setAudioHandler(AudioHandler handler) override;

private:
    void run();
    void publish();
    void disconnectSocket();
    std::optional<AndroidAutoTouchEvent> takePendingTouch();

    EventQueue& events_;
    EndpointProvider endpointProvider_;
    mutable std::mutex mutex_;
    AndroidAutoSession session_;
    StatusHandler statusHandler_;
    VideoHandler videoHandler_;
    AudioHandler audioHandler_;
    std::deque<AndroidAutoTouchEvent> pendingTouches_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<int> activeSocket_{-1};
    std::mutex stopMutex_;
    std::condition_variable stopCondition_;
};

}  // namespace switch_auto::platform::switch_platform
