#pragma once

#include "switch_auto/widget.hpp"

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
    explicit Application(RuntimeService& runtime);

    bool initialize();
    void update(float deltaSeconds);
    void render(Canvas& canvas) const;
    void shutdown();

    ApplicationLifecycle lifecycle() const { return lifecycle_; }

private:
    RuntimeService& runtime_;
    WidgetRegistry registry_;
    ApplicationLifecycle lifecycle_ = ApplicationLifecycle::Created;
};

}  // namespace switch_auto

