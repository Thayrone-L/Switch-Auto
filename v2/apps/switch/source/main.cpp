#include <borealis.hpp>

#include <string>

#include "switch_auto_v2/platform/nvtegra_probe.hpp"

int main() {
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
    if (!brls::Application::init()) return 1;

    const auto probe = switch_auto::v2::platform::probeNvTegra();
    auto* root = new brls::Box(brls::Axis::COLUMN);
    root->setPadding(64, 64, 64, 64);

    auto* title = new brls::Label();
    title->setText("Switch Auto V2 - NVDEC probe");
    title->setFontSize(34);
    root->addView(title);

    auto* status = new brls::Label();
    if (probe.available) {
        status->setText(
            "NVDEC READY\n" + probe.detail +
            "\nNVDEC revision: " + std::to_string(probe.nvdecMajor) + "." +
            std::to_string(probe.nvdecMinor) +
            "\nVIC revision: " + std::to_string(probe.vicMajor) + "." +
            std::to_string(probe.vicMinor));
    } else {
        status->setText("NVDEC FAILED\n" + probe.detail);
    }
    status->setFontSize(26);
    status->setMargins(0, 32, 0, 0);
    root->addView(status);

    brls::Application::pushActivity(new brls::Activity(root));
    while (brls::Application::mainLoop()) {}
    return probe.available ? 0 : 2;
}

