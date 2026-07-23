#include "switch_auto/permissions.hpp"

namespace switch_auto {

void MutablePermissionService::setState(
    Capability capability, PermissionState state) {
    states_[capability] = state;
}

PermissionState MutablePermissionService::state(Capability capability) const {
    const auto found = states_.find(capability);
    return found == states_.end() ? PermissionState::Unknown : found->second;
}

bool MutablePermissionService::canRequest(Capability capability) const {
    const PermissionState current = state(capability);
    return current == PermissionState::Unknown ||
        current == PermissionState::Denied ||
        current == PermissionState::RequiresSettings;
}

void MutablePermissionService::request(
    Capability capability, RequestCallback callback) {
    if (!callback) return;
    callback(state(capability));
}

PermissionDescriptor permissionDescriptor(Capability capability) {
    switch (capability) {
        case Capability::Network: return {capability, "network", false};
        case Capability::Location: return {capability, "location", true};
        case Capability::Motion: return {capability, "motion", false};
        case Capability::MediaControl: return {capability, "media_control", true};
        case Capability::PhoneState: return {capability, "phone_state", true};
        case Capability::AnswerCalls: return {capability, "answer_calls", true};
        case Capability::CallLog: return {capability, "call_log", true};
        case Capability::Notifications: return {capability, "notifications", false};
        case Capability::BackgroundExecution:
            return {capability, "background_execution", false};
        case Capability::Storage: return {capability, "storage", true};
    }
    return {capability, "unknown", true};
}

const char* toString(Capability capability) {
    return permissionDescriptor(capability).id.data();
}

const char* toString(PermissionState state) {
    switch (state) {
        case PermissionState::Unknown: return "unknown";
        case PermissionState::NotRequired: return "not_required";
        case PermissionState::Granted: return "granted";
        case PermissionState::Denied: return "denied";
        case PermissionState::PermanentlyDenied: return "permanently_denied";
        case PermissionState::RequiresSettings: return "requires_settings";
        case PermissionState::Unavailable: return "unavailable";
    }
    return "unknown";
}

}  // namespace switch_auto
