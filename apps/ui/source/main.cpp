#include <borealis.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "dashboard_view.hpp"

#ifdef __SWITCH__
#include <switch.h>

#include "switch_tcp_companion_service.hpp"
#endif

namespace {

class MainActivity final : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/main.xml");
};

#ifdef __SWITCH__
std::optional<std::string> promptText(
    const char* header, const char* guide, const char* initial, std::size_t maximumLength) {
    SwkbdConfig keyboard{};
    if (R_FAILED(swkbdCreate(&keyboard, 0))) return std::nullopt;
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, header);
    swkbdConfigSetGuideText(&keyboard, guide);
    swkbdConfigSetInitialText(&keyboard, initial);
    swkbdConfigSetStringLenMax(&keyboard, maximumLength);

    char output[128]{};
    const Result result = swkbdShow(&keyboard, output, sizeof(output));
    swkbdClose(&keyboard);
    if (R_FAILED(result) || output[0] == '\0') return std::nullopt;
    return std::string(output);
}
#endif

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
    const Result socketResult = socketInitializeDefault();
#endif

    switch_auto::MutableRuntimeService runtime;
    std::unique_ptr<switch_auto::CompanionService> companion;
#ifdef __SWITCH__
    if (R_SUCCEEDED(socketResult)) {
        auto ipAddress = promptText(
            "Android Companion IP", "Exemplo: 10.171.194.141", "", 45);
        auto pairingCode = ipAddress
            ? promptText("Código de pareamento", "Digite os 6 números", "", 6)
            : std::nullopt;
        if (ipAddress && pairingCode) {
            auto tcpService = std::make_unique<
                switch_auto::platform::switch_platform::SwitchTcpCompanionService>();
            const auto started = tcpService->connect({*ipAddress}, *pairingCode);
            if (!started) {
                brls::Logger::error(
                    "Companion connection rejected: {}",
                    switch_auto::toString(started.error()));
            }
            companion = std::move(tcpService);
        }
    } else {
        brls::Logger::error("Unable to initialize Switch sockets: 0x{:x}", socketResult);
    }
#endif
    switch_auto::ui::DashboardView::setRuntimeService(&runtime);
    switch_auto::ui::DashboardView::setCompanionService(companion.get());

    brls::Application::createWindow("Switch Auto");
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(true);
    brls::Application::registerXMLView(
        "DashboardView", switch_auto::ui::DashboardView::create);
    brls::Application::pushActivity(new MainActivity());

    while (brls::Application::mainLoop()) {
    }
#ifdef __SWITCH__
    switch_auto::ui::DashboardView::setCompanionService(nullptr);
    companion.reset();
    if (R_SUCCEEDED(socketResult)) socketExit();
    psmExit();
#endif
    return 0;
}
