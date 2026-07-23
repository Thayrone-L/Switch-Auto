#include "connection_view.hpp"
#include "localization_adapter.hpp"

namespace switch_auto::ui {

using namespace brls::literals;
ConnectionView* ConnectionView::active_ = nullptr;

namespace {

std::string statusText(const CompanionStatus& status) {
    switch (status.state) {
        case CompanionConnectionState::Discovering:
            return tr("switch_auto/status/discovering");
        case CompanionConnectionState::Connecting:
            return tr("switch_auto/status/connecting");
        case CompanionConnectionState::Pairing:
            return tr("switch_auto/status/pairing");
        case CompanionConnectionState::Authenticating:
            return tr("switch_auto/status/authenticating");
        case CompanionConnectionState::Handshaking:
            return tr("switch_auto/status/handshaking");
        case CompanionConnectionState::Online:
            return tr("switch_auto/status/connected");
        case CompanionConnectionState::Reconnecting:
            return brls::getStr("switch_auto/status/retrying",
                status.retryDelaySeconds, status.retryAttempt);
        case CompanionConnectionState::Failed:
            if (status.lastError == CompanionError::DiscoveryTimeout) {
                return tr("switch_auto/connection/not_found");
            }
            return brls::getStr(
                "switch_auto/connection/error", toString(status.lastError));
        case CompanionConnectionState::Disconnected:
            return tr("switch_auto/connection/ready");
    }
    return tr("switch_auto/status/offline");
}

}  // namespace

ConnectionView::ConnectionView(CompanionService& companion) : companion_(companion) {
    active_ = this;
    brls::Logger::info("Connection screen stage: inflating-xml");
    inflateFromXMLRes("xml/views/connection.xml");
    brls::Logger::info("Connection screen stage: xml-ready");
    const auto pairing = companion_.pairingInfo();
    deviceIdLabel_->setText(pairing ? pairing->deviceId : "-");
    qrCode_->setPayload(pairing ? pairing->qrPayload : "");
    connectButton_->setText(tr("switch_auto/connection/connect"));
    offlineButton_->setText(tr("switch_auto/connection/offline"));
    connectButton_->registerClickAction([this](brls::View*) {
        connect();
        return true;
    });
    offlineButton_->registerClickAction([this](brls::View*) {
        close();
        return true;
    });
    refreshStatus(companion_.status());
    brls::Logger::info("Connection screen stage: initialized");
}

ConnectionView::~ConnectionView() {
    if (active_ == this) active_ = nullptr;
}

void ConnectionView::connect() {
    const auto result = companion_.startAutomaticPairing();
    if (!result) {
        statusLabel_->setText(brls::getStr(
            "switch_auto/connection/error", toString(result.error())));
        return;
    }
    connectButton_->setState(brls::ButtonState::DISABLED);
    refreshStatus(companion_.status());
}

void ConnectionView::onCompanionStatus(const CompanionStatus& status) {
    if (active_) active_->refreshStatus(status);
}

void ConnectionView::refreshStatus(const CompanionStatus& status) {
    if (closing_) return;
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
