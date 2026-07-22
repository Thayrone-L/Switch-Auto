#pragma once

#include <string>

namespace switch_auto {

struct RuntimeState {
    int hour = 18;
    int minute = 42;
    int speedKph = 72;
    int switchBatteryPercent = 82;
    int companionBatteryPercent = -1;
    bool companionConnected = false;
    std::string track = "NO MEDIA";
    std::string artist = "ANDROID COMPANION OFFLINE";
};

struct AppState {
    bool initialized = false;
    bool foreground = true;
};

struct WidgetState {
    bool visible = true;
    bool enabled = true;
};

struct PersistentSettings {
    int schemaVersion = 1;
    std::string locale = "en-US";
    std::string theme = "default";
};

struct RuntimeCache {
    RuntimeState runtime;
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
