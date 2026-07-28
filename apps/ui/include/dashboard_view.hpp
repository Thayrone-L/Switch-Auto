#pragma once

#include <borealis.hpp>

#include "switch_auto/companion.hpp"
#include "switch_auto/services.hpp"
#include "switch_auto/widget_layout.hpp"

namespace switch_auto::ui {

class DashboardView final : public brls::Box {
public:
    DashboardView();
    ~DashboardView() override;

    static brls::View* create();
    static void setRuntimeService(MutableRuntimeService* runtime);
    static void setCompanionService(CompanionService* companion);
    static void onCompanionStatus(const CompanionStatus& status);

private:
    void refresh();
    void applyWidgetLayout();

    static MutableRuntimeService* runtime_;
    static CompanionService* companion_;
    static DashboardView* active_;
    CompanionStatus companionStatus_;
    std::uint32_t displayedMapFrameId_ = 0;
    WidgetLayout widgetLayout_ = WidgetLayout::defaultAutomotive();
    brls::RepeatingTimer refreshTimer_;

    BRLS_BIND(brls::Label, clockLabel_, "clock");
    BRLS_BIND(brls::Label, batteryLabel_, "battery");
    BRLS_BIND(brls::Label, speedLabel_, "speed");
    BRLS_BIND(brls::Label, speedUnitLabel_, "speed-unit");
    BRLS_BIND(brls::Label, gpsStatusLabel_, "gps-status");
    BRLS_BIND(brls::Label, gpsCoordinatesLabel_, "gps-coordinates");
    BRLS_BIND(brls::Image, mapImage_, "map-image");
    BRLS_BIND(brls::Box, gpsWidgetBox_, "widget-gps");
    BRLS_BIND(brls::Box, speedWidgetBox_, "widget-speed");
    BRLS_BIND(brls::Box, mediaWidgetBox_, "widget-media");
    BRLS_BIND(brls::Label, connectionLabel_, "connection");
    BRLS_BIND(brls::Button, connectionButton_, "connection-button");
    BRLS_BIND(brls::Label, trackLabel_, "track");
    BRLS_BIND(brls::Label, artistLabel_, "artist");
    BRLS_BIND(brls::Button, mediaPreviousButton_, "media-previous");
    BRLS_BIND(brls::Button, mediaPlayPauseButton_, "media-play-pause");
    BRLS_BIND(brls::Button, mediaNextButton_, "media-next");
};

}  // namespace switch_auto::ui
