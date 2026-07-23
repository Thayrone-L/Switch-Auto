#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "switch_auto/companion.hpp"
#include "switch_device_identity.hpp"

namespace switch_auto::platform::switch_platform {

class SwitchTcpCompanionService final : public CompanionService {
public:
    SwitchTcpCompanionService();
    ~SwitchTcpCompanionService() override;

    Result<void, CompanionError> connect(
        const CompanionEndpoint& endpoint, std::string_view pairingCode) override;
    Result<void, CompanionError> startAutomaticPairing() override;
    void update() override {}
    void disconnect() override;
    CompanionStatus status() const override;
    std::optional<CompanionPairingInfo> pairingInfo() const override;
    bool hasTrustedCompanion() const override { return hasTrustedCompanion_; }

private:
    void runAutomatic();
    void runManual(CompanionEndpoint endpoint, std::string pairingToken);
    CompanionError runSession(const CompanionEndpoint& endpoint,
        const std::string& pairingToken);
    void publish(const CompanionStatus& status);
    bool waitForRetry(std::chrono::milliseconds delay);

    mutable std::mutex statusMutex_;
    CompanionStatus status_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<int> activeSocket_{-1};
    std::mutex waitMutex_;
    std::condition_variable waitCondition_;
    std::optional<SwitchDeviceIdentity> identity_;
    std::atomic<bool> hasTrustedCompanion_{false};
};

}  // namespace switch_auto::platform::switch_platform
