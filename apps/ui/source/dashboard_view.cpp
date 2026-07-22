#include "dashboard_view.hpp"

#include "connection_view.hpp"

#include <cstdio>
#include <ctime>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace switch_auto::ui {

using namespace brls::literals;

MutableRuntimeService* DashboardView::runtime_ = nullptr;
CompanionService* DashboardView::companion_ = nullptr;

namespace {

void updateRuntime(MutableRuntimeService& runtime) {
    auto& state = runtime.mutableState();
    const std::time_t timestamp = std::time(nullptr);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &timestamp);
#else
    localtime_r(&timestamp, &localTime);
#endif
    state.hour = localTime.tm_hour;
    state.minute = localTime.tm_min;

#ifdef __SWITCH__
    u32 battery = 0;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&battery))) {
        state.batteryPercent = static_cast<int>(battery);
    }
    state.speedKph = 0;
#endif
}

}  // namespace

DashboardView::DashboardView() {
    inflateFromXMLRes("xml/views/dashboard.xml");
    connectionButton_->registerClickAction([](brls::View*) {
        CompanionService* service = companion_;
        brls::sync([service]() { openConnectionScreen(service); });
        return true;
    });
    refresh();
    refreshTimer_.setCallback([this]() { refresh(); });
    refreshTimer_.start(1000);
}

DashboardView::~DashboardView() {
    refreshTimer_.stop();
}

brls::View* DashboardView::create() {
    return new DashboardView();
}

void DashboardView::setRuntimeService(MutableRuntimeService* runtime) {
    runtime_ = runtime;
}

void DashboardView::setCompanionService(CompanionService* companion) {
    companion_ = companion;
}

void DashboardView::refresh() {
    if (!runtime_) return;
    updateRuntime(*runtime_);
    auto& mutableState = runtime_->mutableState();
    CompanionConnectionState connectionState = CompanionConnectionState::Disconnected;
    if (companion_) {
        companion_->update();
        connectionState = companion_->status().state;
        mutableState.companionConnected =
            connectionState == CompanionConnectionState::Online;
    }
    const auto& state = runtime_->current();

    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", state.hour, state.minute);
    clockLabel_->setText(buffer);

    std::snprintf(buffer, sizeof(buffer), "%d%%", state.batteryPercent);
    batteryLabel_->setText(buffer);

    std::snprintf(buffer, sizeof(buffer), "%d", state.speedKph);
    speedLabel_->setText(buffer);
    speedUnitLabel_->setText("switch_auto/dashboard/speed_unit"_i18n);
    switch (connectionState) {
        case CompanionConnectionState::Connecting:
            connectionLabel_->setText("switch_auto/status/connecting"_i18n);
            break;
        case CompanionConnectionState::Pairing:
            connectionLabel_->setText("switch_auto/status/pairing"_i18n);
            break;
        case CompanionConnectionState::Handshaking:
            connectionLabel_->setText("switch_auto/status/handshaking"_i18n);
            break;
        case CompanionConnectionState::Online:
            connectionLabel_->setText("switch_auto/status/connected"_i18n);
            break;
        case CompanionConnectionState::Reconnecting:
            connectionLabel_->setText("switch_auto/status/reconnecting"_i18n);
            break;
        case CompanionConnectionState::Failed:
            connectionLabel_->setText("switch_auto/status/failed"_i18n);
            break;
        case CompanionConnectionState::Disconnected:
            connectionLabel_->setText("switch_auto/status/offline"_i18n);
            break;
    }
    connectionButton_->setText(
        state.companionConnected ? "switch_auto/connection/connected"_i18n
                                 : "switch_auto/connection/open"_i18n);
    connectionButton_->setState(
        state.companionConnected ? brls::ButtonState::DISABLED
                                 : brls::ButtonState::ENABLED);
    trackLabel_->setText(state.companionConnected
                             ? state.track
                             : "switch_auto/media/no_media"_i18n);
    artistLabel_->setText(state.companionConnected
                              ? state.artist
                              : "switch_auto/media/companion_offline"_i18n);
}

}  // namespace switch_auto::ui
