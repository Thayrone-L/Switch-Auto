#pragma once

#include <functional>
#include <string_view>
#include <unordered_map>

namespace switch_auto {

enum class Capability {
    Network,
    Location,
    Motion,
    MediaControl,
    PhoneState,
    AnswerCalls,
    CallLog,
    Notifications,
    BackgroundExecution,
    Storage,
};

enum class PermissionState {
    Unknown,
    NotRequired,
    Granted,
    Denied,
    PermanentlyDenied,
    RequiresSettings,
    Unavailable,
};

struct PermissionDescriptor {
    Capability capability;
    std::string_view id;
    bool sensitive;
};

class PermissionService {
public:
    using RequestCallback = std::function<void(PermissionState)>;

    virtual ~PermissionService() = default;
    virtual PermissionState state(Capability capability) const = 0;
    virtual bool canRequest(Capability capability) const = 0;
    virtual void request(Capability capability, RequestCallback callback) = 0;
};

class MutablePermissionService final : public PermissionService {
public:
    void setState(Capability capability, PermissionState state);
    PermissionState state(Capability capability) const override;
    bool canRequest(Capability capability) const override;
    void request(Capability capability, RequestCallback callback) override;

private:
    std::unordered_map<Capability, PermissionState> states_;
};

PermissionDescriptor permissionDescriptor(Capability capability);
const char* toString(Capability capability);
const char* toString(PermissionState state);

}  // namespace switch_auto
