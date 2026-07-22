#pragma once

#include <borealis.hpp>

#include "switch_auto/services.hpp"

namespace switch_auto::ui {

class DashboardView final : public brls::Box {
public:
    DashboardView();
    ~DashboardView() override;

    static brls::View* create();
    static void setRuntimeService(MutableRuntimeService* runtime);

private:
    void refresh();

    static MutableRuntimeService* runtime_;
    brls::RepeatingTimer refreshTimer_;

    BRLS_BIND(brls::Label, clockLabel_, "clock");
    BRLS_BIND(brls::Label, batteryLabel_, "battery");
    BRLS_BIND(brls::Label, speedLabel_, "speed");
    BRLS_BIND(brls::Label, speedUnitLabel_, "speed-unit");
    BRLS_BIND(brls::Label, connectionLabel_, "connection");
    BRLS_BIND(brls::Label, trackLabel_, "track");
    BRLS_BIND(brls::Label, artistLabel_, "artist");
};

}  // namespace switch_auto::ui
