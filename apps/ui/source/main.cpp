#include <borealis.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>
#ifdef __SWITCH__
#include <sys/stat.h>
#endif

#include "connection_view.hpp"
#include "dashboard_view.hpp"
#include "qr_code_view.hpp"

#ifdef __SWITCH__
#include <switch.h>

#include "switch_tcp_companion_service.hpp"
#endif

namespace {

class MainActivity final : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/main.xml");
};

#ifndef __SWITCH__
class SimulatorCompanionService final : public switch_auto::CompanionService {
public:
    switch_auto::Result<void, switch_auto::CompanionError> connect(
        const switch_auto::CompanionEndpoint& endpoint,
        std::string_view pairingCode) override {
        switch_auto::CompanionSession validator;
        if (endpoint.address.empty()) {
            return switch_auto::Result<void, switch_auto::CompanionError>::failure(
                switch_auto::CompanionError::InvalidEndpoint);
        }
        const auto pairing = validator.begin(pairingCode);
        if (!pairing) {
            return switch_auto::Result<void, switch_auto::CompanionError>::failure(
                pairing.error());
        }
        status_.state = switch_auto::CompanionConnectionState::Online;
        status_.lastError = switch_auto::CompanionError::None;
        status_.remoteBatteryPercent = 76;
        return switch_auto::Result<void, switch_auto::CompanionError>::success();
    }
    switch_auto::Result<void, switch_auto::CompanionError> startAutomaticPairing() override {
        status_.state = switch_auto::CompanionConnectionState::Online;
        status_.remoteBatteryPercent = 76;
        return switch_auto::Result<void, switch_auto::CompanionError>::success();
    }
    void update() override {}
    void disconnect() override { status_ = {}; }
    switch_auto::CompanionStatus status() const override { return status_; }
    std::optional<switch_auto::CompanionPairingInfo> pairingInfo() const override {
        return switch_auto::CompanionPairingInfo{
            "SIMULATOR", "123456",
            "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
            "switchauto://pair?v=1&id=SIMULATOR&token=0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"};
    }
    bool hasTrustedCompanion() const override { return true; }

private:
    switch_auto::CompanionStatus status_;
};
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
#ifdef __SWITCH__
    constexpr const char* logPath = "sdmc:/switch/switch-auto/switch-auto.log";
    constexpr const char* previousLogPath = "sdmc:/switch/switch-auto/switch-auto.log.1";
    struct stat logInfo{};
    if (::stat(logPath, &logInfo) == 0 && logInfo.st_size > 1024 * 1024) {
        std::remove(previousLogPath);
        std::rename(logPath, previousLogPath);
    }
    std::FILE* diagnosticLog = std::fopen(logPath, "a");
    if (diagnosticLog) {
        std::setvbuf(diagnosticLog, nullptr, _IONBF, 0);
        brls::Logger::setLogOutput(diagnosticLog);
        brls::Logger::setThreadSafeLogging(true);
        std::fputs("\n--- Switch Auto startup ---\n", diagnosticLog);
    }
#else
    std::FILE* diagnosticLog = nullptr;
#endif
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to initialize Borealis");
        return 1;
    }

    switch_auto::MutableRuntimeService runtime;
    std::unique_ptr<switch_auto::CompanionService> companion;
#ifdef __SWITCH__
    // Borealis' switch_wrapper owns socket/PSM initialization and shutdown.
    companion = std::make_unique<
        switch_auto::platform::switch_platform::SwitchTcpCompanionService>();
#else
    companion = std::make_unique<SimulatorCompanionService>();
#endif
    switch_auto::ui::DashboardView::setRuntimeService(&runtime);
    switch_auto::ui::DashboardView::setCompanionService(companion.get());

    brls::Application::createWindow("Switch Auto");
    brls::Logger::info("Switch Auto stage: window-created");
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(true);
    brls::Application::registerXMLView(
        "DashboardView", switch_auto::ui::DashboardView::create);
    brls::Application::registerXMLView(
        "QrCodeView", switch_auto::ui::QrCodeView::create);
    brls::Logger::info("Switch Auto stage: dashboard-registered");
    brls::Application::pushActivity(new MainActivity());
    brls::Logger::info("Switch Auto stage: dashboard-pushed");
    switch_auto::CompanionService* initialCompanion = companion.get();
    if (initialCompanion && initialCompanion->hasTrustedCompanion()) {
        brls::Logger::info("Switch Auto startup: trusted Companion found, auto-connecting");
        initialCompanion->startAutomaticPairing();
    }
    brls::sync([initialCompanion]() {
        brls::Logger::info("Switch Auto stage: deferred-connection-check");
        if (initialCompanion && !initialCompanion->hasTrustedCompanion() &&
                initialCompanion->status().state ==
                switch_auto::CompanionConnectionState::Disconnected) {
            switch_auto::ui::openConnectionScreen(initialCompanion);
        }
    });

    brls::Logger::info("Switch Auto stage: entering-main-loop");
    while (brls::Application::mainLoop()) {
    }
#ifdef __SWITCH__
    switch_auto::ui::DashboardView::setCompanionService(nullptr);
    companion.reset();
#endif
    if (diagnosticLog) {
        brls::Logger::setLogOutput(nullptr);
        std::fclose(diagnosticLog);
    }
    return 0;
}
