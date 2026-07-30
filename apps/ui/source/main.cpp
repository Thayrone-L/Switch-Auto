#include <borealis.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>
#ifdef __SWITCH__
#include <sys/stat.h>
#endif

#include "connection_view.hpp"
#include "android_auto_video_view.hpp"
#include "dashboard_view.hpp"
#include "qr_code_view.hpp"
#include "localization_adapter.hpp"
#include "switch_auto/event_queue.hpp"

#ifdef __SWITCH__
#include <switch.h>

#include "switch_tcp_companion_service.hpp"
#include "switch_android_auto_host.hpp"
#endif

namespace {

class MainActivity final : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/main.xml");
};

#ifndef __SWITCH__
class SimulatorCompanionService final : public switch_auto::CompanionService {
public:
    explicit SimulatorCompanionService(switch_auto::EventQueue& events)
        : events_(events) {}
    switch_auto::Result<void, switch_auto::CompanionError> connect(
        const switch_auto::CompanionEndpoint& endpoint,
        std::string_view pairingCode) override {
        switch_auto::CompanionSession validator;
        if (endpoint.address.empty()) {
            return switch_auto::Result<void, switch_auto::CompanionError>::failure(
                switch_auto::CompanionError::InvalidEndpoint);
        }
        const auto pairing = validator.begin(pairingCode, "SIMULATOR",
            "0123456789abcdef0123456789abcdef");
        if (!pairing) {
            return switch_auto::Result<void, switch_auto::CompanionError>::failure(
                pairing.error());
        }
        status_.state = switch_auto::CompanionConnectionState::Online;
        status_.lastError = switch_auto::CompanionError::None;
        status_.remoteBatteryPercent = 76;
        publish();
        return switch_auto::Result<void, switch_auto::CompanionError>::success();
    }
    switch_auto::Result<void, switch_auto::CompanionError> startAutomaticPairing() override {
        status_.state = switch_auto::CompanionConnectionState::Online;
        status_.remoteBatteryPercent = 76;
        publish();
        return switch_auto::Result<void, switch_auto::CompanionError>::success();
    }
    void update() override {}
    void disconnect() override { status_ = {}; publish(); }
    switch_auto::CompanionStatus status() const override { return status_; }
    std::optional<switch_auto::CompanionPairingInfo> pairingInfo() const override {
        return switch_auto::CompanionPairingInfo{
            "SIMULATOR", "123456",
            "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
            "switchauto://pair?v=1&id=SIMULATOR&token=0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"};
    }
    bool hasTrustedCompanion() const override { return true; }
    void setStatusHandler(StatusHandler handler) override {
        handler_ = std::move(handler);
    }

private:
    void publish() {
        const auto status = status_;
        events_.post([this, status]() {
            if (handler_) handler_(status);
        });
    }
    switch_auto::EventQueue& events_;
    StatusHandler handler_;
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
    switch_auto::EventQueue events;
    switch_auto::ui::BorealisLocalizationService localization;
    localization.setLocale(brls::Application::getLocale());
    switch_auto::ui::setLocalizationService(&localization);
    std::unique_ptr<switch_auto::CompanionService> companion;
#ifdef __SWITCH__
    std::unique_ptr<switch_auto::AndroidAutoHostService> androidAutoHost;
#endif
#ifdef __SWITCH__
    // Borealis' switch_wrapper owns socket/PSM initialization and shutdown.
    companion = std::make_unique<
        switch_auto::platform::switch_platform::SwitchTcpCompanionService>(events);
    androidAutoHost = std::make_unique<
        switch_auto::platform::switch_platform::SwitchAndroidAutoHost>(
            events, [&companion]() {
                using Host = switch_auto::platform::switch_platform::
                    SwitchAndroidAutoHost;
                if (!companion) return Host::Endpoint{};
                const auto status = companion->status();
                if (!status.remoteAndroidAutoBootstrap ||
                    !status.remoteAndroidAutoBootstrap->ready) {
                    return Host::Endpoint{};
                }
                return Host::Endpoint{
                    status.endpointAddress,
                    status.remoteAndroidAutoBootstrap->port};
            });
#else
    companion = std::make_unique<SimulatorCompanionService>(events);
#endif
    switch_auto::ui::DashboardView::setRuntimeService(&runtime);
    switch_auto::ui::DashboardView::setCompanionService(companion.get());
    companion->setStatusHandler([
#ifdef __SWITCH__
        androidAutoHostPtr = androidAutoHost.get()
#endif
    ](const switch_auto::CompanionStatus& status) {
        switch_auto::ui::DashboardView::onCompanionStatus(status);
        switch_auto::ui::ConnectionView::onCompanionStatus(status);
#ifdef __SWITCH__
        if (androidAutoHostPtr &&
            status.state == switch_auto::CompanionConnectionState::Online &&
            status.remoteAndroidAutoBootstrap &&
            status.remoteAndroidAutoBootstrap->ready &&
            androidAutoHostPtr->status().state ==
                switch_auto::AndroidAutoHostState::Stopped) {
            brls::Logger::info(
                "Android Auto host: Companion bootstrap ready, starting TCP discovery");
            androidAutoHostPtr->start();
        }
#endif
    });
#ifdef __SWITCH__
    switch_auto::ui::AndroidAutoVideoView::setTouchHandler(
        [host = androidAutoHost.get()](
            const switch_auto::AndroidAutoTouchEvent& event) {
            if (host) host->sendTouch(event);
        });
    androidAutoHost->setStatusHandler([](
        const switch_auto::AndroidAutoHostStatus& status) {
        brls::Logger::info("Android Auto host state={} error={} endpoint={}",
            static_cast<int>(status.state), static_cast<int>(status.lastError),
            status.endpointAddress);
    });
#endif

    brls::Application::createWindow("Switch Auto");
    brls::Logger::info("Switch Auto stage: window-created");
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(true);
    brls::Application::registerXMLView(
        "DashboardView", switch_auto::ui::DashboardView::create);
    brls::Application::registerXMLView("AndroidAutoVideoView",
        switch_auto::ui::AndroidAutoVideoView::create);
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
        if (initialCompanion && !initialCompanion->hasTrustedCompanion()) {
            switch_auto::ui::openConnectionScreen(initialCompanion);
        }
    });

#ifdef __SWITCH__
    bool shutdownDone = false;
    auto shutdownServices = [&]() {
        if (shutdownDone) return;
        shutdownDone = true;
        brls::Logger::info("Switch Auto shutdown: begin");
        switch_auto::ui::AndroidAutoVideoView::setTouchHandler({});
        brls::Logger::info("Switch Auto shutdown: touch handler cleared");
        if (androidAutoHost) {
            androidAutoHost->setStatusHandler({});
            androidAutoHost->stop();
            brls::Logger::info(
                "Switch Auto shutdown: Android Auto host stopped");
            androidAutoHost.reset();
            brls::Logger::info(
                "Switch Auto shutdown: Android Auto host destroyed");
        }
        switch_auto::ui::DashboardView::setCompanionService(nullptr);
        if (companion) {
            companion->setStatusHandler({});
            companion->disconnect();
            events.drain();
            brls::Logger::info("Switch Auto shutdown: Companion stopped");
            companion.reset();
            events.drain();
            brls::Logger::info("Switch Auto shutdown: Companion destroyed");
        }
    };
    brls::Application::getExitEvent()->subscribe(shutdownServices);
#endif

    brls::Logger::info("Switch Auto stage: entering-main-loop");
    while (brls::Application::mainLoop()) {
        events.drain();
#ifdef __SWITCH__
        // The bootstrap state may already be available before the status
        // handler is registered, so the callback alone is not a reliable
        // startup trigger. start() is idempotent and changes the state away
        // from Stopped before launching the worker.
        if (androidAutoHost &&
            androidAutoHost->status().state ==
                switch_auto::AndroidAutoHostState::Stopped) {
            const auto status = companion->status();
            if (status.state ==
                    switch_auto::CompanionConnectionState::Online &&
                status.remoteAndroidAutoBootstrap &&
                status.remoteAndroidAutoBootstrap->ready) {
                brls::Logger::info(
                    "Android Auto host: bootstrap ready during main loop, starting");
                androidAutoHost->start();
            }
        }
#endif
    }
#ifdef __SWITCH__
    // The normal path runs from Borealis' exit event, before the Switch
    // platform tears down libnx networking. Keep this as an idempotent
    // fallback for platforms that return from the loop without firing it.
    shutdownServices();
#endif
    if (diagnosticLog) {
        brls::Logger::setLogOutput(nullptr);
        std::fclose(diagnosticLog);
    }
    switch_auto::ui::setLocalizationService(nullptr);
    return 0;
}
