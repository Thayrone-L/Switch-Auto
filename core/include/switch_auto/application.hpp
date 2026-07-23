#pragma once

#include "switch_auto/widget.hpp"
#include "switch_auto/service_registry.hpp"
#include "switch_auto/event_queue.hpp"
#include "switch_auto/localization.hpp"

namespace switch_auto {

enum class ApplicationLifecycle {
    Created,
    InitializingServices,
    RegisteringWidgets,
    StartingUi,
    Running,
    ShuttingDown,
    Stopped,
    Failed,
};

class Application {
public:
    Application(RuntimeService& runtime, LocalizationService& localization);

    bool initialize();
    void update(float deltaSeconds);
    void render(Canvas& canvas) const;
    void shutdown();

    ApplicationLifecycle lifecycle() const { return lifecycle_; }

private:
    RuntimeService& runtime_;
    LocalizationService& localization_;
    WidgetRegistry registry_;
    ServiceRegistry services_;
    AppState appState_;
    EventQueue events_;
    ApplicationLifecycle lifecycle_ = ApplicationLifecycle::Created;
};

}  // namespace switch_auto
