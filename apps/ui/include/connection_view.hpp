#pragma once

#include <borealis.hpp>

#include "switch_auto/companion.hpp"
#include "qr_code_view.hpp"

namespace switch_auto::ui {

class ConnectionView final : public brls::Box {
public:
    explicit ConnectionView(CompanionService& companion);
    ~ConnectionView() override;
    static void onCompanionStatus(const CompanionStatus& status);

private:
    void connect();
    void refreshStatus(const CompanionStatus& status);
    void close();

    CompanionService& companion_;
    static ConnectionView* active_;
    bool closing_ = false;

    BRLS_BIND(brls::Label, deviceIdLabel_, "device-id");
    BRLS_BIND(QrCodeView, qrCode_, "pairing-qr");
    BRLS_BIND(brls::Label, statusLabel_, "connection-status");
    BRLS_BIND(brls::Button, connectButton_, "connect-button");
    BRLS_BIND(brls::Button, offlineButton_, "offline-button");
};

void openConnectionScreen(CompanionService* companion);

}  // namespace switch_auto::ui
