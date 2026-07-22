#include "switch_auto/application.hpp"

namespace switch_auto {

Application::Application(RuntimeService& runtime) : runtime_(runtime) {}

bool Application::initialize() {
    if (lifecycle_ != ApplicationLifecycle::Created) return false;
    lifecycle_ = ApplicationLifecycle::InitializingServices;
    if (!services_.registerService<RuntimeService>(runtime_)) {
        lifecycle_ = ApplicationLifecycle::Failed;
        return false;
    }
    lifecycle_ = ApplicationLifecycle::RegisteringWidgets;
    if (!registry_.registerWidget(makeClockWidget()) ||
        !registry_.registerWidget(makeBatteryWidget()) ||
        !registry_.registerWidget(makeDriveWidget()) ||
        !registry_.registerWidget(makeMediaWidget())) {
        lifecycle_ = ApplicationLifecycle::Failed;
        return false;
    }
    if (!registry_.initializeAll({runtime_, services_})) {
        lifecycle_ = ApplicationLifecycle::Failed;
        return false;
    }
    lifecycle_ = ApplicationLifecycle::StartingUi;
    lifecycle_ = ApplicationLifecycle::Running;
    appState_.initialized = true;
    return true;
}

void Application::update(float deltaSeconds) {
    if (lifecycle_ == ApplicationLifecycle::Running) {
        events_.drain();
        registry_.updateAll(deltaSeconds);
    }
}

void Application::render(Canvas& canvas) const {
    canvas.clear({13, 17, 24});
    canvas.fillRect(0, 0, kLogicalWidth, 136, {17, 22, 31});
    canvas.fillRect(0, 134, kLogicalWidth, 2, {230, 0, 18});
    canvas.text(48, 38, "SWITCH AUTO", 6, {238, 241, 246});
    canvas.text(50, 96, "DEVELOPMENT CORE", 2, {230, 0, 18});
    registry_.renderAll(canvas);
}

void Application::shutdown() {
    if (lifecycle_ == ApplicationLifecycle::Stopped) return;
    lifecycle_ = ApplicationLifecycle::ShuttingDown;
    registry_.disposeAll();
    appState_.initialized = false;
    lifecycle_ = ApplicationLifecycle::Stopped;
}

}  // namespace switch_auto
