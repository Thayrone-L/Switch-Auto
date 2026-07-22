#pragma once

#include <array>
#include <string>

#include "switch_auto/companion.hpp"

namespace switch_auto::platform::switch_platform {

class SwitchDeviceIdentity {
public:
    static Result<SwitchDeviceIdentity, CompanionError> loadOrCreate();

    const CompanionPairingInfo& pairingInfo() const { return pairingInfo_; }

private:
    std::array<unsigned char, 32> secret_{};
    CompanionPairingInfo pairingInfo_;
};

}  // namespace switch_auto::platform::switch_platform
