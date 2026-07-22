#pragma once

#include <string>

namespace switch_auto {

struct RuntimeState {
    int hour = 18;
    int minute = 42;
    int speedKph = 72;
    int batteryPercent = 82;
    bool companionConnected = false;
    std::string track = "NO MEDIA";
    std::string artist = "ANDROID COMPANION OFFLINE";
};

class RuntimeService {
public:
    virtual ~RuntimeService() = default;
    virtual const RuntimeState& current() const = 0;
};

class MutableRuntimeService final : public RuntimeService {
public:
    const RuntimeState& current() const override { return state_; }
    RuntimeState& mutableState() { return state_; }

private:
    RuntimeState state_;
};

}  // namespace switch_auto

