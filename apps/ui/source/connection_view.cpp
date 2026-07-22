#include "connection_view.hpp"

namespace switch_auto::ui {

using namespace brls::literals;

namespace {

std::string statusText(const CompanionStatus& status) {
    switch (status.state) {
        case CompanionConnectionState::Discovering:
            return "switch_auto/status/discovering"_i18n;
        case CompanionConnectionState::Connecting:
            return "switch_auto/status/connecting"_i18n;
        case CompanionConnectionState::Pairing:
            return "switch_auto/status/pairing"_i18n;
        case CompanionConnectionState::Handshaking:
            return "switch_auto/status/handshaking"_i18n;
        case CompanionConnectionState::Online:
            return "switch_auto/status/connected"_i18n;
        case CompanionConnectionState::Reconnecting:
            return "switch_auto/status/reconnecting"_i18n;
        case CompanionConnectionState::Failed:
            if (status.lastError == CompanionError::DiscoveryTimeout) {
                return "switch_auto/connection/not_found"_i18n;
            }
            return brls::getStr(
                "switch_auto/connection/error", toString(status.lastError));
        case CompanionConnectionState::Disconnected:
            return "switch_auto/connection/ready"_i18n;
    }
    return "switch_auto/status/offline"_i18n;
}

}  // namespace

ConnectionView::ConnectionView(CompanionService& companion) : companion_(companion) {
    brls::Logger::info("Connection screen stage: inflating-xml");
    inflateFromXMLRes("xml/views/connection.xml");
    brls::Logger::info("Connection screen stage: xml-ready");
    const auto pairing = companion_.pairingInfo();
    deviceIdLabel_->setText(pairing ? pairing->deviceId : "-");
    qrCode_->setPayload(pairing ? pairing->qrPayload : "");
    connectButton_->setText("switch_auto/connection/connect"_i18n);
    offlineButton_->setText("switch_auto/connection/offline"_i18n);
    connectButton_->registerClickAction([this](brls::View*) {
        connect();
        return true;
    });
    offlineButton_->registerClickAction([this](brls::View*) {
        close();
        return true;
    });
    statusTimer_.setCallback([this]() { refreshStatus(); });
    statusTimer_.start(250);
    refreshStatus();
    brls::Logger::info("Connection screen stage: initialized");
}

ConnectionView::~ConnectionView() {
    statusTimer_.stop();
}

void ConnectionView::connect() {
    const auto result = companion_.startAutomaticPairing();
    if (!result) {
        statusLabel_->setText(brls::getStr(
            "switch_auto/connection/error", toString(result.error())));
        return;
    }
    connectButton_->setState(brls::ButtonState::DISABLED);
    refreshStatus();
}

void ConnectionView::refreshStatus() {
    if (closing_) return;
    const CompanionStatus status = companion_.status();
    statusLabel_->setText(statusText(status));
    if (status.state == CompanionConnectionState::Online) {
        close();
    } else if (status.state == CompanionConnectionState::Failed ||
               status.state == CompanionConnectionState::Disconnected) {
        connectButton_->setState(brls::ButtonState::ENABLED);
    }
}

void ConnectionView::close() {
    if (closing_) return;
    closing_ = true;
    statusTimer_.stop();
    brls::Application::popActivity();
}

void openConnectionScreen(CompanionService* companion) {
    if (!companion) {
        brls::Logger::error("Connection screen requested without CompanionService");
        return;
    }
    brls::Logger::info("Connection screen stage: constructing");
    auto* view = new ConnectionView(*companion);
    brls::Logger::info("Connection screen stage: pushing-activity");
    brls::Application::pushActivity(new brls::Activity(view));
    brls::Logger::info("Connection screen stage: activity-pushed");
}

}  // namespace switch_auto::ui
