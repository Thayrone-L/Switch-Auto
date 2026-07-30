#include "dashboard_view.hpp"

#include "connection_view.hpp"
#include "localization_adapter.hpp"

#include <cstdio>
#include <ctime>

#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#endif

namespace switch_auto::ui {

using namespace brls::literals;

MutableRuntimeService* DashboardView::runtime_ = nullptr;
CompanionService* DashboardView::companion_ = nullptr;
DashboardView* DashboardView::active_ = nullptr;

namespace {

#ifdef __SWITCH__
constexpr const char* kLayoutDirectory = "sdmc:/switch/switch-auto/config";
constexpr const char* kLayoutPath =
    "sdmc:/switch/switch-auto/config/widget-layout.json";
constexpr const char* kLayoutTemporaryPath =
    "sdmc:/switch/switch-auto/config/widget-layout.json.tmp";
constexpr const char* kLayoutCorruptPath =
    "sdmc:/switch/switch-auto/config/widget-layout.json.corrupt";

WidgetLayout loadWidgetLayout() {
    WidgetLayout fallback = WidgetLayout::defaultAutomotive();
    std::FILE* file = std::fopen(kLayoutPath, "rb");
    if (!file) {
        ::mkdir(kLayoutDirectory, 0700);
        if (std::FILE* output = std::fopen(kLayoutTemporaryPath, "wb")) {
            const std::string payload = fallback.serialize();
            std::fwrite(payload.data(), 1, payload.size(), output);
            std::fclose(output);
            std::rename(kLayoutTemporaryPath, kLayoutPath);
        }
        return fallback;
    }
    std::string payload;
    char buffer[1024];
    while (payload.size() <= 16 * 1024) {
        const std::size_t count = std::fread(buffer, 1, sizeof(buffer), file);
        payload.append(buffer, count);
        if (count < sizeof(buffer)) break;
    }
    std::fclose(file);
    const auto restored = payload.size() <= 16 * 1024
        ? WidgetLayout::deserialize(payload) : std::nullopt;
    if (restored) return *restored;
    std::remove(kLayoutCorruptPath);
    std::rename(kLayoutPath, kLayoutCorruptPath);
    brls::Logger::error("Widget layout: corrupt file backed up; defaults restored");
    return fallback;
}
#else
WidgetLayout loadWidgetLayout() {
    return WidgetLayout::defaultAutomotive();
}
#endif

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
#endif
}

}  // namespace

DashboardView::DashboardView() {
    active_ = this;
    widgetLayout_ = loadWidgetLayout();
    inflateFromXMLRes("xml/views/dashboard.xml");
    applyWidgetLayout();
    gpsWidgetBox_->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound*) {
            AndroidAutoTouchAction action = AndroidAutoTouchAction::Up;
            switch (status.state) {
                case brls::GestureState::UNSURE:
                    return;
                case brls::GestureState::START:
                case brls::GestureState::STAY:
                    action = AndroidAutoTouchAction::Move;
                    break;
                case brls::GestureState::END:
                case brls::GestureState::FAILED:
                case brls::GestureState::INTERRUPTED:
                    action = AndroidAutoTouchAction::Up;
                    break;
            }
            mapImage_->dispatchTouch(action, status.position);
        },
        brls::PanAxis::ANY));
    gpsWidgetBox_->addGestureRecognizer(new brls::TapGestureRecognizer(
        [this](brls::TapGestureStatus status, brls::Sound*) {
            if (status.state == brls::GestureState::UNSURE ||
                status.state == brls::GestureState::START) {
                mapImage_->dispatchTouch(
                    AndroidAutoTouchAction::Down, status.position);
            } else if (status.state == brls::GestureState::END) {
                mapImage_->dispatchTouch(
                    AndroidAutoTouchAction::Up, status.position);
            }
        }));
    connectionButton_->registerClickAction([](brls::View*) {
        CompanionService* service = companion_;
        brls::sync([service]() { openConnectionScreen(service); });
        return true;
    });
    mediaPreviousButton_->registerClickAction([](brls::View*) {
        if (companion_) companion_->controlMedia("previous");
        return true;
    });
    mediaPlayPauseButton_->registerClickAction([](brls::View*) {
        if (companion_) companion_->controlMedia("play_pause");
        return true;
    });
    mediaNextButton_->registerClickAction([](brls::View*) {
        if (companion_) companion_->controlMedia("next");
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

void DashboardView::applyWidgetLayout() {
    // Android Auto is the primary surface. Keep legacy widgets inflated for
    // their bindings, but remove their entire column from layout.
    gpsWidgetBox_->setVisibility(brls::Visibility::VISIBLE);
    gpsWidgetBox_->setWidthPercentage(100.0f);
    gpsWidgetBox_->setGrow(1.0f);
    if (auto* legacyColumn = speedWidgetBox_->getParent()) {
        legacyColumn->setVisibility(brls::Visibility::GONE);
        legacyColumn->setGrow(0.0f);
    }
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
    if (status.remoteCustomization) {
        const auto layout = WidgetLayout::deserialize(
            status.remoteCustomization->layoutJson);
        if (layout) {
            active_->widgetLayout_ = *layout;
            active_->applyWidgetLayout();
        }
    }
    if (runtime_) {
        auto& state = runtime_->mutableState();
        state.companionBatteryPercent = status.remoteBatteryPercent.value_or(-1);
        state.companionConnected = status.state == CompanionConnectionState::Online;
        state.speedKph = status.remoteLocation && status.remoteLocation->speedMetersPerSecond
            ? static_cast<int>(*status.remoteLocation->speedMetersPerSecond * 3.6 + 0.5)
            : 0;
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
    if (companionStatus_.remoteLocation) {
        const auto& location = *companionStatus_.remoteLocation;
        gpsStatusLabel_->setText(brls::getStr("switch_auto/gps/accuracy",
            static_cast<int>(location.accuracyMeters + 0.5)));
        std::snprintf(buffer, sizeof(buffer), "%.5f, %.5f",
            location.latitude, location.longitude);
        gpsCoordinatesLabel_->setText(buffer);
        if (companionStatus_.remoteMapFrame &&
            companionStatus_.remoteMapFrame->id != displayedMapFrameId_ &&
            !companionStatus_.remoteMapFrame->data.empty()) {
            const auto& frame = *companionStatus_.remoteMapFrame;
            mapImage_->setImageFromMem(
                frame.data.data(), static_cast<int>(frame.data.size()));
            mapImage_->setScalingType(brls::ImageScalingType::FILL);
            displayedMapFrameId_ = frame.id;
        }
    } else {
        gpsStatusLabel_->setText(tr("switch_auto/gps/waiting"));
        gpsCoordinatesLabel_->setText(tr("switch_auto/gps/no_position"));
        mapImage_->clear();
        displayedMapFrameId_ = 0;
    }
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
    connectionButton_->setText("●");
    connectionButton_->setTextColor(state.companionConnected
        ? nvgRGB(57, 211, 137) : nvgRGB(110, 124, 145));
    connectionButton_->setState(brls::ButtonState::ENABLED);
    trackLabel_->setText(companionStatus_.remoteMedia
        ? companionStatus_.remoteMedia->title
        : tr("switch_auto/media/no_media"));
    artistLabel_->setText(companionStatus_.remoteMedia
        ? companionStatus_.remoteMedia->artist
        : (state.companionConnected
            ? tr("switch_auto/media/no_media")
            : tr("switch_auto/media/companion_offline")));
    mediaPlayPauseButton_->setText(
        companionStatus_.remoteMedia && companionStatus_.remoteMedia->playing
            ? "PAUSE" : "PLAY");
}

}  // namespace switch_auto::ui
