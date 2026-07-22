#include <borealis.hpp>

#include <filesystem>

#include "dashboard_view.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace {

class MainActivity final : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/main.xml");
};

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc > 0) {
        std::filesystem::current_path(
            std::filesystem::absolute(argv[0]).parent_path());
    }
#else
    (void)argc;
    (void)argv;
#endif
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to initialize Borealis");
        return 1;
    }

#ifdef __SWITCH__
    psmInitialize();
#endif

    switch_auto::MutableRuntimeService runtime;
    switch_auto::ui::DashboardView::setRuntimeService(&runtime);

    brls::Application::createWindow("Switch Auto");
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(true);
    brls::Application::registerXMLView(
        "DashboardView", switch_auto::ui::DashboardView::create);
    brls::Application::pushActivity(new MainActivity());

    while (brls::Application::mainLoop()) {
    }
#ifdef __SWITCH__
    psmExit();
#endif
    return 0;
}
