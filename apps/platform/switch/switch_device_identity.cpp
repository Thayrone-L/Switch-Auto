#include "switch_device_identity.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include <switch.h>

namespace switch_auto::platform::switch_platform {
namespace {

constexpr const char* kIdentityDirectory = "sdmc:/switch/switch-auto/identity";
constexpr const char* kIdentityPath = "sdmc:/switch/switch-auto/identity/device.identity";
constexpr const char* kIdentityTemporaryPath =
    "sdmc:/switch/switch-auto/identity/device.identity.tmp";
constexpr const char* kIdentityCorruptPath =
    "sdmc:/switch/switch-auto/identity/device.identity.corrupt";
constexpr const char* kLegacyIdentityPath =
    "sdmc:/switch/switch-auto/identity/device.key";
constexpr unsigned char kIdentityHeader[] = {'S', 'A', 'I', 'D', 1};

enum class ReadResult { Missing, Valid, Corrupt };

ReadResult readSecret(std::array<unsigned char, 32>& secret) {
    std::FILE* file = std::fopen(kIdentityPath, "rb");
    if (!file) return ReadResult::Missing;
    unsigned char header[sizeof(kIdentityHeader)]{};
    const bool valid =
        std::fread(header, 1, sizeof(header), file) == sizeof(header) &&
        std::memcmp(header, kIdentityHeader, sizeof(header)) == 0 &&
        std::fread(secret.data(), 1, secret.size(), file) == secret.size() &&
        std::fgetc(file) == EOF;
    std::fclose(file);
    return valid ? ReadResult::Valid : ReadResult::Corrupt;
}

bool saveSecret(const std::array<unsigned char, 32>& secret) {
    ::mkdir(kIdentityDirectory, 0700);
    std::FILE* file = std::fopen(kIdentityTemporaryPath, "wb");
    if (!file) return false;
    const bool saved =
        std::fwrite(kIdentityHeader, 1, sizeof(kIdentityHeader), file) ==
            sizeof(kIdentityHeader) &&
        std::fwrite(secret.data(), 1, secret.size(), file) == secret.size();
    std::fflush(file);
    std::fclose(file);
    if (!saved) return false;
    std::remove(kIdentityPath);
    return std::rename(kIdentityTemporaryPath, kIdentityPath) == 0;
}

bool readLegacySecret(std::array<unsigned char, 32>& secret) {
    std::FILE* file = std::fopen(kLegacyIdentityPath, "rb");
    if (!file) return false;
    const bool valid =
        std::fread(secret.data(), 1, secret.size(), file) == secret.size() &&
        std::fgetc(file) == EOF;
    std::fclose(file);
    return valid;
}

bool createSecret(std::array<unsigned char, 32>& secret) {
    if (R_FAILED(csrngInitialize())) return false;
    const ::Result randomResult = csrngGetRandomBytes(secret.data(), secret.size());
    csrngExit();
    if (R_FAILED(randomResult)) return false;

    return saveSecret(secret);
}

}  // namespace

Result<SwitchDeviceIdentity, CompanionError> SwitchDeviceIdentity::loadOrCreate() {
    SwitchDeviceIdentity identity;
    const ReadResult stored = readSecret(identity.secret_);
    if (stored == ReadResult::Corrupt) {
        std::remove(kIdentityCorruptPath);
        std::rename(kIdentityPath, kIdentityCorruptPath);
    }
    if (stored != ReadResult::Valid && readLegacySecret(identity.secret_)) {
        if (!saveSecret(identity.secret_)) {
            return Result<SwitchDeviceIdentity, CompanionError>::failure(
                CompanionError::TransportUnavailable);
        }
    } else if (stored != ReadResult::Valid && !createSecret(identity.secret_)) {
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
