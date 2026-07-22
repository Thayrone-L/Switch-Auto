#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "switch_auto/companion.hpp"

namespace switch_auto::platform::switch_platform {

class SwitchTcpCompanionService final : public CompanionService {
public:
    SwitchTcpCompanionService() = default;
    ~SwitchTcpCompanionService() override;

    Result<void, CompanionError> connect(
        const CompanionEndpoint& endpoint, std::string_view pairingCode) override;
    void update() override {}
    void disconnect() override;
    CompanionStatus status() const override;

private:
    void run(CompanionEndpoint endpoint, std::string pairingCode);
    void publish(const CompanionStatus& status);
    bool waitForRetry();

    mutable std::mutex statusMutex_;
    CompanionStatus status_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::mutex waitMutex_;
    std::condition_variable waitCondition_;
};

}  // namespace switch_auto::platform::switch_platform
