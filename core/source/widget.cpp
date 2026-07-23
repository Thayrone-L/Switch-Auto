#include "switch_auto/widget.hpp"
#include "switch_auto/localization.hpp"

#include <algorithm>
#include <cstdio>

namespace switch_auto {

bool WidgetRegistry::registerWidget(std::unique_ptr<Widget> widget) {
    if (!widget) return false;
    const auto duplicate = std::find_if(entries_.begin(), entries_.end(),
        [&](const Entry& entry) { return entry.widget->id() == widget->id(); });
    if (duplicate != entries_.end()) return false;
    entries_.push_back({std::move(widget), WidgetLifecycle::Registered});
    return true;
}

bool WidgetRegistry::initializeAll(const WidgetContext& context) {
    for (Entry& entry : entries_) {
        if (!entry.widget->initialize(context)) return false;
        entry.lifecycle = WidgetLifecycle::Initialized;
        if (!entry.widget->activate()) return false;
        entry.lifecycle = WidgetLifecycle::Active;
    }
    return true;
}

bool WidgetRegistry::suspend(std::string_view id) {
    const auto entry = std::find_if(entries_.begin(), entries_.end(),
        [id](const Entry& value) { return value.widget->id() == id; });
    if (entry == entries_.end() || entry->lifecycle != WidgetLifecycle::Active) return false;
    entry->widget->suspend();
    entry->lifecycle = WidgetLifecycle::Suspended;
    return true;
}

bool WidgetRegistry::resume(std::string_view id) {
    const auto entry = std::find_if(entries_.begin(), entries_.end(),
        [id](const Entry& value) { return value.widget->id() == id; });
    if (entry == entries_.end() || entry->lifecycle != WidgetLifecycle::Suspended) return false;
    entry->widget->resume();
    entry->lifecycle = WidgetLifecycle::Active;
    return true;
}

WidgetLifecycle WidgetRegistry::lifecycle(std::string_view id) const {
    const auto entry = std::find_if(entries_.begin(), entries_.end(),
        [id](const Entry& value) { return value.widget->id() == id; });
    return entry == entries_.end() ? WidgetLifecycle::Disposed : entry->lifecycle;
}

void WidgetRegistry::updateAll(float deltaSeconds) {
    for (Entry& entry : entries_) {
        if (entry.lifecycle == WidgetLifecycle::Active) entry.widget->update(deltaSeconds);
    }
}

void WidgetRegistry::renderAll(Canvas& canvas) const {
    for (const Entry& entry : entries_) {
        if (entry.lifecycle == WidgetLifecycle::Active) entry.widget->render(canvas);
    }
}

void WidgetRegistry::disposeAll() {
    for (Entry& entry : entries_) {
        if (entry.lifecycle != WidgetLifecycle::Disposed) {
            entry.widget->dispose();
            entry.lifecycle = WidgetLifecycle::Disposed;
        }
    }
}

namespace {

class RuntimeWidget : public Widget {
public:
    bool initialize(const WidgetContext& context) override {
        runtime_ = &context.runtime;
        localization_ = context.services.find<LocalizationService>();
        return localization_ != nullptr;
    }
    void update(float) override {}
    void dispose() override { runtime_ = nullptr; localization_ = nullptr; }

protected:
    const RuntimeState& state() const { return runtime_->current(); }
    std::string tr(std::string_view key) const {
        return localization_->translate(key);
    }
    RuntimeService* runtime_ = nullptr;
    LocalizationService* localization_ = nullptr;
};

class ClockWidget final : public RuntimeWidget {
public:
    std::string_view id() const override { return "core.clock"; }
    void render(Canvas& canvas) const override {
        char time[16];
        std::snprintf(time, sizeof(time), "%02d:%02d", state().hour, state().minute);
        canvas.text(1060, 34, time, 4, {238, 241, 246});

    }
};

class BatteryWidget final : public RuntimeWidget {
public:
    std::string_view id() const override { return "core.battery"; }
    void render(Canvas& canvas) const override {
        char switchBattery[16];
        std::snprintf(switchBattery, sizeof(switchBattery), "SW %d%%",
            state().switchBatteryPercent);
        canvas.text(1030, 92, switchBattery, 2, {160, 170, 188});
        canvas.strokeRect(1048, 88, 62, 28, 3, {160, 170, 188});
        canvas.fillRect(1110, 96, 5, 12, {160, 170, 188});
        canvas.fillRect(1054, 94, state().switchBatteryPercent / 2, 16,
                        state().switchBatteryPercent > 20 ? Color{49, 209, 139} : Color{239, 83, 80});
        char phoneBattery[24];
        if (state().companionBatteryPercent >= 0) {
            std::snprintf(phoneBattery, sizeof(phoneBattery), "PHONE %d%%",
                state().companionBatteryPercent);
        } else {
            std::snprintf(phoneBattery, sizeof(phoneBattery), "PHONE --");
        }
        canvas.text(850, 92, phoneBattery, 2,
            state().companionBatteryPercent >= 0 ? Color{49, 209, 139} : Color{102, 117, 140});
    }
};

class DriveWidget final : public RuntimeWidget {
public:
    std::string_view id() const override { return "core.drive"; }
    void render(Canvas& canvas) const override {
        canvas.fillRect(48, 166, 742, 482, {22, 28, 39});
        canvas.strokeRect(48, 166, 742, 482, 2, {48, 59, 76});
        canvas.text(82, 202, tr("core/drive/title"), 3, {102, 117, 140});

        char speed[8];
        std::snprintf(speed, sizeof(speed), "%d", state().speedKph);
        canvas.text(102, 302, speed, 13, {238, 241, 246});
        canvas.text(112, 410, tr("core/drive/speed_unit"), 3, {102, 117, 140});

        canvas.fillRect(410, 246, 2, 300, {48, 59, 76});
        canvas.text(462, 282, tr("core/drive/next_turn"), 2, {102, 117, 140});
        canvas.text(462, 334, tr("core/drive/route_offline"), 4, {238, 241, 246});
        canvas.text(462, 408, tr("core/drive/connect_android"), 2, {239, 83, 80});
    }
};

class MediaWidget final : public RuntimeWidget {
public:
    std::string_view id() const override { return "core.media"; }
    void render(Canvas& canvas) const override {
        canvas.fillRect(822, 166, 410, 482, {22, 28, 39});
        canvas.strokeRect(822, 166, 410, 482, 2, {48, 59, 76});
        canvas.text(856, 202, tr("core/media/now_playing"), 3, {102, 117, 140});
        canvas.fillRect(856, 270, 342, 150, {31, 39, 53});
        canvas.text(886, 326, state().companionConnected
                    ? tr("core/media/android") : tr("core/media/offline"), 5,
                    state().companionConnected ? Color{49, 209, 139} : Color{102, 117, 140});
        canvas.text(856, 462, state().track, 3, {238, 241, 246});
        canvas.text(856, 508, state().artist, 2, {102, 117, 140});
        canvas.text(884, 580, tr("core/media/previous"), 2, {160, 170, 188});
        canvas.text(1000, 580, tr("core/media/play"), 2, {238, 241, 246});
        canvas.text(1110, 580, tr("core/media/next"), 2, {160, 170, 188});
    }
};

}  // namespace

std::unique_ptr<Widget> makeClockWidget() { return std::make_unique<ClockWidget>(); }
std::unique_ptr<Widget> makeBatteryWidget() { return std::make_unique<BatteryWidget>(); }
std::unique_ptr<Widget> makeDriveWidget() { return std::make_unique<DriveWidget>(); }
std::unique_ptr<Widget> makeMediaWidget() { return std::make_unique<MediaWidget>(); }

}  // namespace switch_auto
