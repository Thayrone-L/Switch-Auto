#include "dashboard_view.hpp"

#include <cstdio>
#include <ctime>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace switch_auto::ui {

using namespace brls::literals;

MutableRuntimeService* DashboardView::runtime_ = nullptr;

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

void DashboardView::refresh() {
    if (!runtime_) return;
    updateRuntime(*runtime_);
    const auto& state = runtime_->current();

    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", state.hour, state.minute);
    clockLabel_->setText(buffer);

    std::snprintf(buffer, sizeof(buffer), "%d%%", state.batteryPercent);
    batteryLabel_->setText(buffer);

    std::snprintf(buffer, sizeof(buffer), "%d", state.speedKph);
    speedLabel_->setText(buffer);
    speedUnitLabel_->setText("switch_auto/dashboard/speed_unit"_i18n);
    connectionLabel_->setText(
        state.companionConnected ? "switch_auto/status/connected"_i18n
                                 : "switch_auto/status/offline"_i18n);
    trackLabel_->setText(state.companionConnected
                             ? state.track
                             : "switch_auto/media/no_media"_i18n);
    artistLabel_->setText(state.companionConnected
                              ? state.artist
                              : "switch_auto/media/companion_offline"_i18n);
}

}  // namespace switch_auto::ui
