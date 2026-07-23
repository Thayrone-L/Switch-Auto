#include "dashboard_view.hpp"

#include "connection_view.hpp"
#include "localization_adapter.hpp"

#include <cstdio>
#include <ctime>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace switch_auto::ui {

using namespace brls::literals;

MutableRuntimeService* DashboardView::runtime_ = nullptr;
CompanionService* DashboardView::companion_ = nullptr;
DashboardView* DashboardView::active_ = nullptr;

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
        state.switchBatteryPercent = static_cast<int>(battery);
    }
    state.speedKph = 0;
#endif
}

}  // namespace

DashboardView::DashboardView() {
    active_ = this;
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
    if (active_ == this) active_ = nullptr;
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

void DashboardView::onCompanionStatus(const CompanionStatus& status) {
    if (!active_) return;
    active_->companionStatus_ = status;
    if (runtime_) {
        auto& state = runtime_->mutableState();
        state.companionBatteryPercent = status.remoteBatteryPercent.value_or(-1);
        state.companionConnected = status.state == CompanionConnectionState::Online;
    }
    active_->refresh();
}

void DashboardView::refresh() {
    if (!runtime_) return;
    updateRuntime(*runtime_);
    const CompanionConnectionState connectionState = companionStatus_.state;
    const auto& state = runtime_->current();

    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", state.hour, state.minute);
    clockLabel_->setText(buffer);

    char phoneBattery[16] = "--";
    if (state.companionBatteryPercent >= 0) {
        std::snprintf(phoneBattery, sizeof(phoneBattery), "%d%%",
            state.companionBatteryPercent);
    }
    std::snprintf(buffer, sizeof(buffer), "SW %d%%  |  PHONE %s",
        state.switchBatteryPercent, phoneBattery);
    batteryLabel_->setText(buffer);

    std::snprintf(buffer, sizeof(buffer), "%d", state.speedKph);
    speedLabel_->setText(buffer);
    speedUnitLabel_->setText(tr("switch_auto/dashboard/speed_unit"));
    switch (connectionState) {
        case CompanionConnectionState::Discovering:
            connectionLabel_->setText(tr("switch_auto/status/discovering"));
            break;
        case CompanionConnectionState::Connecting:
            connectionLabel_->setText(tr("switch_auto/status/connecting"));
            break;
        case CompanionConnectionState::Pairing:
            connectionLabel_->setText(tr("switch_auto/status/pairing"));
            break;
        case CompanionConnectionState::Authenticating:
            connectionLabel_->setText(tr("switch_auto/status/authenticating"));
            break;
        case CompanionConnectionState::Handshaking:
            connectionLabel_->setText(tr("switch_auto/status/handshaking"));
            break;
        case CompanionConnectionState::Online:
            connectionLabel_->setText(tr("switch_auto/status/connected"));
            break;
        case CompanionConnectionState::Reconnecting:
            connectionLabel_->setText(brls::getStr("switch_auto/status/retrying",
                companionStatus_.retryDelaySeconds, companionStatus_.retryAttempt));
            break;
        case CompanionConnectionState::Failed:
            connectionLabel_->setText(tr("switch_auto/status/failed"));
            break;
        case CompanionConnectionState::Disconnected:
            connectionLabel_->setText(tr("switch_auto/status/offline"));
            break;
    }
    connectionButton_->setText(
        state.companionConnected ? tr("switch_auto/connection/connected")
                                 : tr("switch_auto/connection/open"));
    connectionButton_->setState(
        state.companionConnected ? brls::ButtonState::DISABLED
                                 : brls::ButtonState::ENABLED);
    trackLabel_->setText(state.companionConnected
                             ? state.track
                             : tr("switch_auto/media/no_media"));
    artistLabel_->setText(state.companionConnected
                              ? state.artist
                              : tr("switch_auto/media/companion_offline"));
}

}  // namespace switch_auto::ui
