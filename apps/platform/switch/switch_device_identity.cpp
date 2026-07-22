#include "switch_device_identity.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include <switch.h>

namespace switch_auto::platform::switch_platform {
namespace {

constexpr const char* kIdentityDirectory = "sdmc:/switch/switch-auto/identity";
constexpr const char* kIdentityPath = "sdmc:/switch/switch-auto/identity/device.key";

bool readSecret(std::array<unsigned char, 32>& secret) {
    std::FILE* file = std::fopen(kIdentityPath, "rb");
    if (!file) return false;
    const bool valid = std::fread(secret.data(), 1, secret.size(), file) == secret.size();
    std::fclose(file);
    return valid;
}

bool createSecret(std::array<unsigned char, 32>& secret) {
    ::mkdir(kIdentityDirectory, 0700);
    if (R_FAILED(csrngInitialize())) return false;
    const ::Result randomResult = csrngGetRandomBytes(secret.data(), secret.size());
    csrngExit();
    if (R_FAILED(randomResult)) return false;

    std::FILE* file = std::fopen(kIdentityPath, "wb");
    if (!file) return false;
    const bool saved = std::fwrite(secret.data(), 1, secret.size(), file) == secret.size();
    std::fclose(file);
    return saved;
}

}  // namespace

Result<SwitchDeviceIdentity, CompanionError> SwitchDeviceIdentity::loadOrCreate() {
    SwitchDeviceIdentity identity;
    if (!readSecret(identity.secret_) && !createSecret(identity.secret_)) {
        return Result<SwitchDeviceIdentity, CompanionError>::failure(
            CompanionError::TransportUnavailable);
    }

    std::array<unsigned char, SHA256_HASH_SIZE> hash{};
    sha256CalculateHash(hash.data(), identity.secret_.data(), identity.secret_.size());

    char deviceId[17]{};
    for (std::size_t index = 0; index < 8; ++index) {
        std::snprintf(deviceId + index * 2, 3, "%02X", hash[index]);
    }
    const unsigned value =
        (static_cast<unsigned>(hash[8]) << 24U) |
        (static_cast<unsigned>(hash[9]) << 16U) |
        (static_cast<unsigned>(hash[10]) << 8U) |
        static_cast<unsigned>(hash[11]);
    char code[7]{};
    std::snprintf(code, sizeof(code), "%06u", value % 1'000'000U);
    char token[65]{};
    for (std::size_t index = 0; index < identity.secret_.size(); ++index) {
        std::snprintf(token + index * 2, 3, "%02X", identity.secret_[index]);
    }
    identity.pairingInfo_ = {deviceId, code, token,
        "switchauto://pair?v=1&id=" + std::string(deviceId) + "&token=" + token};
    return Result<SwitchDeviceIdentity, CompanionError>::success(std::move(identity));
}

}  // namespace switch_auto::platform::switch_platform
