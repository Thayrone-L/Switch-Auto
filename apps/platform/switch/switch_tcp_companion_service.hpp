#pragma once

#include <atomic>
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

private:
    void run(CompanionEndpoint endpoint, std::string pairingCode);
    void runDiscovery();
    void publish(const CompanionStatus& status);
    bool waitForRetry();

    mutable std::mutex statusMutex_;
    CompanionStatus status_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::mutex waitMutex_;
    std::condition_variable waitCondition_;
    std::optional<SwitchDeviceIdentity> identity_;
};

}  // namespace switch_auto::platform::switch_platform
