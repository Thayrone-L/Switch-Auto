#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "switch_auto/canvas.hpp"
#include "switch_auto/services.hpp"

namespace switch_auto {

enum class WidgetLifecycle {
    Registered,
    Initialized,
    Active,
    Suspended,
    Disposed,
};

struct WidgetContext {
    RuntimeService& runtime;
};

class Widget {
public:
    virtual ~Widget() = default;
    virtual std::string_view id() const = 0;
    virtual bool initialize(const WidgetContext& context) = 0;
    virtual void update(float deltaSeconds) = 0;
    virtual void render(Canvas& canvas) const = 0;
    virtual void dispose() = 0;
};

class WidgetRegistry {
public:
    bool registerWidget(std::unique_ptr<Widget> widget);
    bool initializeAll(const WidgetContext& context);
    void updateAll(float deltaSeconds);
    void renderAll(Canvas& canvas) const;
    void disposeAll();
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::unique_ptr<Widget> widget;
        WidgetLifecycle lifecycle = WidgetLifecycle::Registered;
    };

    std::vector<Entry> entries_;
};

std::unique_ptr<Widget> makeClockWidget();
std::unique_ptr<Widget> makeDriveWidget();
std::unique_ptr<Widget> makeMediaWidget();

}  // namespace switch_auto

