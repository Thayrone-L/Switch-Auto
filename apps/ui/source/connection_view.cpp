#include "connection_view.hpp"

namespace switch_auto::ui {

using namespace brls::literals;

namespace {

std::string statusText(const CompanionStatus& status) {
    switch (status.state) {
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
            return brls::getStr(
                "switch_auto/connection/error", toString(status.lastError));
        case CompanionConnectionState::Disconnected:
            return "switch_auto/connection/ready"_i18n;
    }
    return "switch_auto/status/offline"_i18n;
}

}  // namespace

ConnectionView::ConnectionView(CompanionService& companion) : companion_(companion) {
    inflateFromXMLRes("xml/views/connection.xml");
    ipInput_->init(
        "switch_auto/connection/ip_label"_i18n, "", {},
        "switch_auto/connection/ip_placeholder"_i18n,
        "switch_auto/connection/ip_hint"_i18n, 45);
    codeInput_->init(
        "switch_auto/connection/code_label"_i18n, "", {},
        "switch_auto/connection/code_placeholder"_i18n,
        "switch_auto/connection/code_hint"_i18n, 6);
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
}

ConnectionView::~ConnectionView() {
    statusTimer_.stop();
}

void ConnectionView::connect() {
    const auto result = companion_.connect(
        {ipInput_->getValue(), kCompanionPort}, codeInput_->getValue());
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
    if (!companion) return;
    brls::Application::pushActivity(
        new brls::Activity(new ConnectionView(*companion)));
}

}  // namespace switch_auto::ui
