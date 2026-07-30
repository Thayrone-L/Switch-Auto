#include "switch_tcp_companion_service.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <borealis.hpp>
#include <switch.h>

#include "switch_auto/widget_layout.hpp"

namespace switch_auto::platform::switch_platform {
namespace {

constexpr int kSocketTimeoutSeconds = 8;
constexpr auto kConnectTimeout = std::chrono::seconds(8);
constexpr auto kConnectPollInterval = std::chrono::milliseconds(100);
constexpr std::uint16_t kDiscoveryPort = 38472;
constexpr int kDiscoveryAttempts = 4;
constexpr int kDiscoveryTimeoutSeconds = 1;
constexpr auto kMinimumReconnectDelay = std::chrono::seconds(1);
constexpr auto kMaximumReconnectDelay = std::chrono::seconds(16);
constexpr int kBackoffJitterPercent = 20;
constexpr auto kHeartbeatInterval = std::chrono::milliseconds(200);
constexpr std::size_t kMaximumMessageSize = 4096;
constexpr const char* kTrustedCompanionPath =
    "sdmc:/switch/switch-auto/identity/companion.json";
constexpr const char* kTrustedCompanionTemporaryPath =
    "sdmc:/switch/switch-auto/identity/companion.json.tmp";
constexpr const char* kTrustedCompanionCorruptPath =
    "sdmc:/switch/switch-auto/identity/companion.json.corrupt";
constexpr const char* kLegacyTrustedCompanionPath =
    "sdmc:/switch/switch-auto/identity/companion.trusted";
constexpr const char* kDiagnosticDirectory =
    "sdmc:/switch/switch-auto/diagnostics";
constexpr const char* kDiagnosticPath =
    "sdmc:/switch/switch-auto/diagnostics/latest.json";
constexpr const char* kConfigDirectory = "sdmc:/switch/switch-auto/config";
constexpr const char* kActiveCustomizationPath =
    "sdmc:/switch/switch-auto/config/active-customization.json";
constexpr const char* kActiveCustomizationTemporaryPath =
    "sdmc:/switch/switch-auto/config/active-customization.json.tmp";
constexpr const char* kWidgetLayoutPath =
    "sdmc:/switch/switch-auto/config/widget-layout.json";
constexpr const char* kWidgetLayoutTemporaryPath =
    "sdmc:/switch/switch-auto/config/widget-layout.json.tmp";

bool validIdentifier(std::string_view value) {
    return !value.empty() && value.size() <= 64 &&
        std::all_of(value.begin(), value.end(), [](char character) {
            return std::isalnum(static_cast<unsigned char>(character)) ||
                character == '-' || character == '_' || character == '.';
        });
}

std::uint32_t loadAppliedCustomizationRevision() {
    std::FILE* file = std::fopen(kActiveCustomizationPath, "rb");
    if (!file) return 0;
    char buffer[512]{};
    const std::size_t count = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    const char* revision = std::strstr(buffer, "\"revision\":");
    if (!revision || count == 0) return 0;
    unsigned int value = 0;
    return std::sscanf(revision, "\"revision\":%u", &value) == 1 ? value : 0;
}

bool applyCustomization(const RemoteCustomization& customization) {
    if (!validIdentifier(customization.themeId) ||
        !validIdentifier(customization.layoutId) ||
        customization.themeId != "default-dark") return false;
    const auto layout = WidgetLayout::deserialize(customization.layoutJson);
    if (!layout) return false;
    ::mkdir(kConfigDirectory, 0700);

    std::FILE* layoutFile = std::fopen(kWidgetLayoutTemporaryPath, "wb");
    if (!layoutFile) return false;
    const std::string normalizedLayout = layout->serialize();
    const bool layoutWritten = std::fwrite(normalizedLayout.data(), 1,
        normalizedLayout.size(), layoutFile) == normalizedLayout.size();
    std::fflush(layoutFile);
    std::fclose(layoutFile);
    if (!layoutWritten) return false;

    std::FILE* manifestFile =
        std::fopen(kActiveCustomizationTemporaryPath, "wb");
    if (!manifestFile) {
        std::remove(kWidgetLayoutTemporaryPath);
        return false;
    }
    const std::string manifest =
        "{\"schemaVersion\":1,\"revision\":" +
        std::to_string(customization.revision) + ",\"themeId\":\"" +
        customization.themeId + "\",\"layoutId\":\"" +
        customization.layoutId + "\"}\n";
    const bool manifestWritten = std::fwrite(manifest.data(), 1,
        manifest.size(), manifestFile) == manifest.size();
    std::fflush(manifestFile);
    std::fclose(manifestFile);
    if (!manifestWritten) {
        std::remove(kWidgetLayoutTemporaryPath);
        return false;
    }

    std::remove(kWidgetLayoutPath);
    if (std::rename(kWidgetLayoutTemporaryPath, kWidgetLayoutPath) != 0) {
        std::remove(kActiveCustomizationTemporaryPath);
        return false;
    }
    std::remove(kActiveCustomizationPath);
    return std::rename(
        kActiveCustomizationTemporaryPath, kActiveCustomizationPath) == 0;
}

bool saveTrustedCompanion() {
    std::FILE* file = std::fopen(kTrustedCompanionTemporaryPath, "wb");
    if (!file) return false;
    const char payload[] = "{\"schemaVersion\":1,\"trusted\":true}\n";
    const bool written = std::fwrite(payload, 1, sizeof(payload) - 1, file) ==
        sizeof(payload) - 1;
    std::fflush(file);
    std::fclose(file);
    if (!written) return false;
    std::remove(kTrustedCompanionPath);
    return std::rename(kTrustedCompanionTemporaryPath, kTrustedCompanionPath) == 0;
}

bool loadTrustedCompanion() {
    if (std::FILE* file = std::fopen(kTrustedCompanionPath, "rb")) {
        char buffer[160]{};
        const std::size_t size = std::fread(buffer, 1, sizeof(buffer) - 1, file);
        std::fclose(file);
        const bool valid = size > 0 && std::strstr(buffer, "\"schemaVersion\":1") &&
            std::strstr(buffer, "\"trusted\":true");
        if (valid) return true;
        std::remove(kTrustedCompanionCorruptPath);
        std::rename(kTrustedCompanionPath, kTrustedCompanionCorruptPath);
        brls::Logger::error("Companion settings: corrupt state backed up and ignored");
        return false;
    }
    if (std::FILE* legacy = std::fopen(kLegacyTrustedCompanionPath, "rb")) {
        std::fclose(legacy);
        if (saveTrustedCompanion()) {
            brls::Logger::info("Companion settings: migrated trusted state to schema 1");
        }
        return true;
    }
    return false;
}

void writeDiagnosticReport(std::string_view sessionId,
    const CompanionEndpoint& endpoint, const CompanionStatus& status) {
    ::mkdir(kDiagnosticDirectory, 0700);
    std::FILE* file = std::fopen(kDiagnosticPath, "wb");
    if (!file) return;
    std::fprintf(file,
        "{\"schemaVersion\":1,\"platform\":\"switch\",\"protocolVersion\":%d,"
        "\"sessionId\":\"%.*s\",\"endpoint\":\"%s\",\"state\":\"%s\","
        "\"lastError\":\"%s\",\"retryAttempt\":%u,\"lastSequence\":%u}\n",
        kProtocolVersion, static_cast<int>(sessionId.size()), sessionId.data(),
        endpoint.address.c_str(), toString(status.state), toString(status.lastError),
        status.retryAttempt, status.lastSequence);
    std::fclose(file);
}

bool sendAll(int socketFd, std::string_view payload) {
    while (!payload.empty()) {
        const ssize_t sent = ::send(socketFd, payload.data(), payload.size(), 0);
        if (sent <= 0) return false;
        payload.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
}

bool receiveLine(int socketFd, std::string& output) {
    output.clear();
    while (output.size() < kMaximumMessageSize) {
        char value = '\0';
        const ssize_t received = ::recv(socketFd, &value, 1, 0);
        if (received <= 0) return false;
        if (value == '\n') return true;
        if (value != '\r') output.push_back(value);
    }
    return false;
}

bool receiveExact(int socketFd, std::vector<std::uint8_t>& output, std::size_t size) {
    output.resize(size);
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t received = ::recv(
            socketFd, output.data() + offset, size - offset, 0);
        if (received <= 0) return false;
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

int openSocket(const CompanionEndpoint& endpoint,
    const std::atomic<bool>& stopRequested, CompanionError& failure) {
    failure = CompanionError::TransportUnavailable;
    const int socketFd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketFd < 0) return -1;

    const int originalFlags = ::fcntl(socketFd, F_GETFL, 0);
    if (originalFlags < 0 ||
        ::fcntl(socketFd, F_SETFL, originalFlags | O_NONBLOCK) < 0) {
        ::close(socketFd);
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    if (::inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr) != 1) {
        ::close(socketFd);
        return -1;
    }
    if (::connect(socketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 &&
        errno != EINPROGRESS) {
        ::close(socketFd);
        return -1;
    }

    const auto deadline = std::chrono::steady_clock::now() + kConnectTimeout;
    bool connected = false;
    while (!stopRequested && std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{socketFd, POLLOUT, 0};
        const int result = ::poll(&descriptor, 1,
            static_cast<int>(kConnectPollInterval.count()));
        if (result < 0 && errno != EINTR) break;
        if (result > 0) {
            int socketError = 0;
            socklen_t errorLength = sizeof(socketError);
            if (::getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &socketError,
                    &errorLength) == 0 && socketError == 0) {
                connected = true;
            } else {
                errno = socketError;
            }
            break;
        }
    }
    if (!connected && !stopRequested &&
        std::chrono::steady_clock::now() >= deadline) {
        failure = CompanionError::ConnectionTimeout;
    }
    if (!connected || stopRequested ||
        ::fcntl(socketFd, F_SETFL, originalFlags) < 0) {
        ::close(socketFd);
        return -1;
    }

    timeval timeout{};
    timeout.tv_sec = kSocketTimeoutSeconds;
    ::setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    return socketFd;
}

CompanionError socketFailure() {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT
        ? CompanionError::ConnectionTimeout
        : CompanionError::ConnectionClosed;
}

std::chrono::milliseconds reconnectDelay(
    std::chrono::seconds base, std::uint32_t attempt) {
    // Stable per-attempt jitter avoids synchronized reconnect storms without
    // requiring another platform RNG initialization.
    const int span = static_cast<int>(base.count() * 1000) *
        kBackoffJitterPercent / 100;
    const std::uint32_t mixed = attempt * 1103515245U + 12345U;
    const int offset = span == 0 ? 0 :
        static_cast<int>(mixed % static_cast<std::uint32_t>(span * 2 + 1)) - span;
    return std::chrono::milliseconds(base.count() * 1000 + offset);
}

bool permanentProtocolError(CompanionError error) {
    return error == CompanionError::InvalidPairingCode ||
           error == CompanionError::PairingRejected ||
           error == CompanionError::AuthenticationFailed ||
           error == CompanionError::UnsupportedVersion ||
           error == CompanionError::InvalidMessage;
}

std::optional<std::string> createNonce() {
    std::array<unsigned char, 16> bytes{};
    if (R_FAILED(csrngInitialize())) return std::nullopt;
    const ::Result result = csrngGetRandomBytes(bytes.data(), bytes.size());
    csrngExit();
    if (R_FAILED(result)) return std::nullopt;
    constexpr char alphabet[] = "0123456789abcdef";
    std::string nonce(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        nonce[index * 2] = alphabet[bytes[index] >> 4];
        nonce[index * 2 + 1] = alphabet[bytes[index] & 0x0f];
    }
    return nonce;
}

std::optional<CompanionEndpoint> discoverCompanion(
    const CompanionPairingInfo& identity, const std::atomic<bool>& stopRequested) {
    const int socketFd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketFd < 0) {
        brls::Logger::error("Companion discovery: socket creation failed errno={}", errno);
        return std::nullopt;
    }
    int enabled = 1;
    ::setsockopt(socketFd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));
    timeval timeout{};
    timeout.tv_sec = kDiscoveryTimeoutSeconds;
    ::setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in globalTarget{};
    globalTarget.sin_family = AF_INET;
    globalTarget.sin_port = htons(kDiscoveryPort);
    globalTarget.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    // Some Android hotspots do not forward 255.255.255.255, but accept their
    // subnet broadcast. Infer the current /24 address from the selected route.
    sockaddr_in subnetTarget = globalTarget;
    sockaddr_in routeTarget{};
    routeTarget.sin_family = AF_INET;
    routeTarget.sin_port = htons(9);
    ::inet_pton(AF_INET, "8.8.8.8", &routeTarget.sin_addr);
    if (::connect(socketFd, reinterpret_cast<sockaddr*>(&routeTarget),
            sizeof(routeTarget)) == 0) {
        sockaddr_in local{};
        socklen_t localLength = sizeof(local);
        if (::getsockname(socketFd, reinterpret_cast<sockaddr*>(&local), &localLength) == 0) {
            subnetTarget.sin_addr.s_addr = local.sin_addr.s_addr | htonl(0x000000FFU);
        }
        sockaddr_in unconnected{};
        unconnected.sin_family = AF_UNSPEC;
        ::connect(socketFd, reinterpret_cast<sockaddr*>(&unconnected), sizeof(unconnected));
    }
    const std::string probe =
        "{\"version\":" + std::to_string(kProtocolVersion) +
        ",\"type\":\"discover.request\",\"deviceId\":\"" +
        identity.deviceId + "\"}\n";

    std::optional<CompanionEndpoint> endpoint;
    for (int attempt = 0; attempt < kDiscoveryAttempts && !stopRequested && !endpoint;
         ++attempt) {
        brls::Logger::info("Companion discovery: probe {}/{}", attempt + 1,
            kDiscoveryAttempts);
        ::sendto(socketFd, probe.data(), probe.size(), 0,
            reinterpret_cast<sockaddr*>(&globalTarget), sizeof(globalTarget));
        if (subnetTarget.sin_addr.s_addr != globalTarget.sin_addr.s_addr) {
            ::sendto(socketFd, probe.data(), probe.size(), 0,
                reinterpret_cast<sockaddr*>(&subnetTarget), sizeof(subnetTarget));
        }
        char buffer[512]{};
        sockaddr_in sender{};
        socklen_t senderLength = sizeof(sender);
        const ssize_t received = ::recvfrom(socketFd, buffer, sizeof(buffer) - 1, 0,
            reinterpret_cast<sockaddr*>(&sender), &senderLength);
        if (received > 0 && std::strstr(buffer, "\"type\":\"discover.reply\"") &&
            std::strstr(buffer,
                ("\"version\":" + std::to_string(kProtocolVersion)).c_str())) {
            char address[INET_ADDRSTRLEN]{};
            if (::inet_ntop(AF_INET, &sender.sin_addr, address, sizeof(address))) {
                endpoint = CompanionEndpoint{address, kCompanionPort};
                brls::Logger::info("Companion discovery: reply from {}:{}", address,
                    kCompanionPort);
            }
        }
    }
    ::close(socketFd);
    return endpoint;
}

}  // namespace

SwitchTcpCompanionService::SwitchTcpCompanionService(EventQueue& events)
    : events_(events) {
    appliedCustomizationRevision_ = loadAppliedCustomizationRevision();
    auto identity = SwitchDeviceIdentity::loadOrCreate();
    if (identity) {
        identity_ = std::move(identity.value());
        hasTrustedCompanion_ = loadTrustedCompanion();
        brls::Logger::info("Companion identity ready: deviceId={}",
            identity_->pairingInfo().deviceId);
    } else {
        brls::Logger::error("Companion identity failed: {}", toString(identity.error()));
        CompanionStatus failed;
        failed.state = CompanionConnectionState::Failed;
        failed.lastError = identity.error();
        publish(failed);
    }
}

SwitchTcpCompanionService::~SwitchTcpCompanionService() {
    stopTransport(false);
}

Result<void, CompanionError> SwitchTcpCompanionService::startAutomaticPairing() {
    if (!identity_) {
        return Result<void, CompanionError>::failure(CompanionError::TransportUnavailable);
    }
    disconnect();
    brls::Logger::info("Companion pairing: automatic discovery requested");
    stopRequested_ = false;
    CompanionStatus discovering;
    discovering.state = CompanionConnectionState::Discovering;
    publish(discovering);
    worker_ = std::thread(&SwitchTcpCompanionService::runAutomatic, this);
    return Result<void, CompanionError>::success();
}

Result<void, CompanionError> SwitchTcpCompanionService::connect(
    const CompanionEndpoint& endpoint, std::string_view pairingCode) {
    in_addr parsedAddress{};
    if (endpoint.port == 0 ||
        ::inet_pton(AF_INET, endpoint.address.c_str(), &parsedAddress) != 1) {
        return Result<void, CompanionError>::failure(CompanionError::InvalidEndpoint);
    }
    CompanionSession validator;
    if (!validator.begin(pairingCode, "manual", "00000000000000000000000000000000")) {
        return Result<void, CompanionError>::failure(CompanionError::InvalidPairingCode);
    }

    disconnect();
    stopRequested_ = false;
    CompanionStatus connecting;
    connecting.state = CompanionConnectionState::Connecting;
    connecting.endpointAddress = endpoint.address;
    publish(connecting);
    worker_ = std::thread(&SwitchTcpCompanionService::runManual, this, endpoint,
        std::string(pairingCode));
    return Result<void, CompanionError>::success();
}

void SwitchTcpCompanionService::disconnect() {
    stopTransport(true);
}

void SwitchTcpCompanionService::stopTransport(bool publishDisconnected) {
    stopRequested_ = true;
    waitCondition_.notify_all();
    const int socketFd = activeSocket_.load();
    if (socketFd >= 0) ::shutdown(socketFd, SHUT_RDWR);
    if (worker_.joinable()) worker_.join();
    brls::Logger::info("Companion transport: disconnected");
    if (publishDisconnected) publish({});
}

CompanionStatus SwitchTcpCompanionService::status() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}

std::optional<CompanionPairingInfo> SwitchTcpCompanionService::pairingInfo() const {
    if (!identity_) return std::nullopt;
    return identity_->pairingInfo();
}

void SwitchTcpCompanionService::runAutomatic() {
    const CompanionPairingInfo info = identity_->pairingInfo();
    auto delay = kMinimumReconnectDelay;
    std::uint32_t retryAttempt = 0;
    while (!stopRequested_) {
        CompanionStatus discovering;
        discovering.state = CompanionConnectionState::Discovering;
        discovering.retryAttempt = retryAttempt;
        publish(discovering);
        const auto endpoint = discoverCompanion(info, stopRequested_);
        if (stopRequested_) return;

        CompanionError failure = CompanionError::DiscoveryTimeout;
        if (endpoint) {
            failure = runSession(*endpoint, info.pairingToken);
            if (stopRequested_) return;
            if (permanentProtocolError(failure)) {
                CompanionStatus failed;
                failed.state = CompanionConnectionState::Failed;
                failed.lastError = failure;
                failed.endpointAddress = endpoint->address;
                publish(failed);
                return;
            }
            delay = kMinimumReconnectDelay;
        } else {
            brls::Logger::error("Companion discovery: timed out after {} attempts",
                kDiscoveryAttempts);
        }

        ++retryAttempt;
        CompanionStatus reconnecting;
        reconnecting.state = CompanionConnectionState::Reconnecting;
        reconnecting.lastError = failure;
        reconnecting.retryAttempt = retryAttempt;
        const auto actualDelay = reconnectDelay(delay, retryAttempt);
        reconnecting.retryDelaySeconds = static_cast<std::uint32_t>(
            (actualDelay.count() + 999) / 1000);
        publish(reconnecting);
        brls::Logger::info("Companion reconnect: attempt={} delay={}ms reason={}",
            retryAttempt, actualDelay.count(), toString(failure));
        if (!waitForRetry(actualDelay)) return;
        delay = std::min(delay * 2, kMaximumReconnectDelay);
    }
}

void SwitchTcpCompanionService::runManual(
    CompanionEndpoint endpoint, std::string pairingToken) {
    auto delay = kMinimumReconnectDelay;
    std::uint32_t retryAttempt = 0;
    while (!stopRequested_) {
        const CompanionError failure = runSession(endpoint, pairingToken);
        if (stopRequested_ || permanentProtocolError(failure)) return;
        if (!waitForRetry(reconnectDelay(delay, ++retryAttempt))) return;
        delay = std::min(delay * 2, kMaximumReconnectDelay);
    }
}

CompanionError SwitchTcpCompanionService::runSession(
    const CompanionEndpoint& endpoint, const std::string& pairingToken) {
    const auto generatedSessionId = createNonce();
    const std::string sessionId = generatedSessionId
        ? generatedSessionId->substr(0, 12) : "unavailable";
    CompanionSession session;
    session.transportConnected();
    CompanionStatus connecting = session.status();
    connecting.sessionId = sessionId;
    connecting.endpointAddress = endpoint.address;
    publish(connecting);
    const auto publishSession = [this, &session, &endpoint, &sessionId]() {
        CompanionStatus status = session.status();
        status.sessionId = sessionId;
        status.endpointAddress = endpoint.address;
        publish(status);
    };

    CompanionError openFailure = CompanionError::TransportUnavailable;
    const int socketFd = openSocket(endpoint, stopRequested_, openFailure);
    if (socketFd < 0) {
        if (!stopRequested_) {
            brls::Logger::error("[session={}] Companion TCP: connection to {}:{} failed error={} errno={}",
                sessionId, endpoint.address, endpoint.port, toString(openFailure), errno);
        }
        CompanionStatus diagnostic = connecting;
        diagnostic.lastError = openFailure;
        diagnostic.state = CompanionConnectionState::Failed;
        writeDiagnosticReport(sessionId, endpoint, diagnostic);
        return stopRequested_ ? CompanionError::None : openFailure;
    }
    activeSocket_ = socketFd;
    if (stopRequested_) ::shutdown(socketFd, SHUT_RDWR);
    brls::Logger::info("[session={}] Companion TCP: connected to {}:{}",
        sessionId, endpoint.address, endpoint.port);

    CompanionError failure = CompanionError::None;
    const auto clientNonce = createNonce();
    if (!clientNonce) {
        int expectedSocket = socketFd;
        activeSocket_.compare_exchange_strong(expectedSocket, -1);
        ::close(socketFd);
        return CompanionError::TransportUnavailable;
    }
    const auto pairRequest = session.begin(
        pairingToken, identity_ ? identity_->pairingInfo().deviceId : "", *clientNonce,
        sessionId);
    publishSession();
    std::string response;

    if (!pairRequest || !sendAll(socketFd, pairRequest.value())) {
        failure = pairRequest ? socketFailure() : pairRequest.error();
    } else {
        brls::Logger::info("[session={}] Companion protocol: pair.request sent deviceId={}",
            sessionId, identity_ ? identity_->pairingInfo().deviceId : "unknown");
        while (failure == CompanionError::None &&
               session.status().state != CompanionConnectionState::Online) {
            if (!receiveLine(socketFd, response)) {
                failure = socketFailure();
                break;
            }
            const auto next = session.receive(response);
            publishSession();
            if (!next) {
                failure = next.error();
            } else if (next.value() && !sendAll(socketFd, *next.value())) {
                failure = socketFailure();
            }
        }
        if (failure == CompanionError::None) {
            brls::Logger::info("[session={}] Companion protocol: mutually authenticated session online",
                sessionId);
            hasTrustedCompanion_ = true;
            if (!saveTrustedCompanion()) {
                brls::Logger::error("Companion settings: failed to persist trusted state");
            }
        }
    }

    while (failure == CompanionError::None && !stopRequested_) {
            std::unique_lock<std::mutex> lock(waitMutex_);
            if (waitCondition_.wait_for(lock, kHeartbeatInterval,
                    [this] { return stopRequested_.load(); })) break;
            lock.unlock();

            u32 batteryPercent = 0;
            const std::optional<int> localBattery =
                R_SUCCEEDED(psmGetBatteryChargePercentage(&batteryPercent))
                ? std::optional<int>(static_cast<int>(batteryPercent))
                : std::nullopt;
            const std::uint32_t customizationRevision =
                appliedCustomizationRevision_.load();
            float mapPanX = 0.0f;
            float mapPanY = 0.0f;
            std::string mediaAction;
            {
                std::lock_guard<std::mutex> lock(mapCommandMutex_);
                mapPanX = pendingMapPanX_;
                mapPanY = pendingMapPanY_;
                pendingMapPanX_ = 0.0f;
                pendingMapPanY_ = 0.0f;
                mediaAction = std::move(pendingMediaAction_);
                pendingMediaAction_.clear();
            }
            const auto ping = session.heartbeat(localBattery,
                customizationRevision > 0
                    ? std::optional<std::uint32_t>(customizationRevision)
                    : std::nullopt,
                mapPanX != 0.0f || mapPanY != 0.0f
                    ? std::optional<float>(mapPanX) : std::nullopt,
                mapPanX != 0.0f || mapPanY != 0.0f
                    ? std::optional<float>(mapPanY) : std::nullopt,
                !mediaAction.empty()
                    ? std::optional<std::string_view>(mediaAction)
                    : std::nullopt);
            if (!ping || !sendAll(socketFd, ping.value()) ||
                !receiveLine(socketFd, response)) {
                failure = ping ? socketFailure() : ping.error();
                break;
            }
            const auto pong = session.receive(response);
            if (!pong) {
                failure = pong.error();
            } else if (const auto binaryLength = session.pendingBinaryLength()) {
                std::vector<std::uint8_t> binaryPayload;
                if (!receiveExact(socketFd, binaryPayload, *binaryLength)) {
                    failure = socketFailure();
                } else {
                    const auto accepted = session.acceptBinaryPayload(
                        std::move(binaryPayload));
                    if (!accepted) failure = accepted.error();
                }
            }
            if (failure == CompanionError::None) {
                const auto customization =
                    session.status().remoteCustomization;
                if (customization &&
                    customization->revision !=
                        appliedCustomizationRevision_.load()) {
                    if (applyCustomization(*customization)) {
                        appliedCustomizationRevision_ = customization->revision;
                        brls::Logger::info(
                            "[session={}] Customization applied revision={} theme={} layout={}",
                            sessionId, customization->revision,
                            customization->themeId, customization->layoutId);
                    } else {
                        brls::Logger::error(
                            "[session={}] Customization rejected revision={}",
                            sessionId, customization->revision);
                    }
                }
            }
            publishSession();
        }

    ::shutdown(socketFd, SHUT_RDWR);
    int expectedSocket = socketFd;
    activeSocket_.compare_exchange_strong(expectedSocket, -1);
    ::close(socketFd);
    if (stopRequested_) return CompanionError::None;
    if (failure == CompanionError::None) failure = CompanionError::ConnectionClosed;
    session.transportDisconnected(failure);
    brls::Logger::error("[session={}] Companion transport: session ended ({})",
        sessionId, toString(failure));
    publishSession();
    CompanionStatus diagnosticStatus = session.status();
    diagnosticStatus.sessionId = sessionId;
    writeDiagnosticReport(sessionId, endpoint, diagnosticStatus);
    return failure;
}

void SwitchTcpCompanionService::publish(const CompanionStatus& status) {
    bool scheduleEvent = false;
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        status_ = status;
        ++statusGeneration_;
        if (!statusEventScheduled_) {
            statusEventScheduled_ = true;
            scheduleEvent = true;
        }
    }
    if (scheduleEvent) {
        events_.post([this]() { dispatchLatestStatus(); });
    }
}

void SwitchTcpCompanionService::dispatchLatestStatus() {
    CompanionStatus latest;
    std::uint64_t dispatchedGeneration = 0;
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        latest = status_;
        dispatchedGeneration = statusGeneration_;
    }

    StatusHandler handler;
    {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        handler = statusHandler_;
    }
    if (handler) handler(latest);

    bool scheduleNext = false;
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        if (statusGeneration_ == dispatchedGeneration) {
            statusEventScheduled_ = false;
        } else {
            scheduleNext = true;
        }
    }
    if (scheduleNext) {
        events_.post([this]() { dispatchLatestStatus(); });
    }
}

void SwitchTcpCompanionService::setStatusHandler(StatusHandler handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    statusHandler_ = std::move(handler);
}

void SwitchTcpCompanionService::panMap(
    float deltaXPixels, float deltaYPixels) {
    std::lock_guard<std::mutex> lock(mapCommandMutex_);
    pendingMapPanX_ += deltaXPixels;
    pendingMapPanY_ += deltaYPixels;
}

void SwitchTcpCompanionService::controlMedia(std::string_view action) {
    if (action != "play_pause" && action != "previous" && action != "next") return;
    std::lock_guard<std::mutex> lock(mapCommandMutex_);
    pendingMediaAction_ = action;
}

bool SwitchTcpCompanionService::waitForRetry(std::chrono::milliseconds delay) {
    std::unique_lock<std::mutex> lock(waitMutex_);
    return !waitCondition_.wait_for(lock, delay,
        [this] { return stopRequested_.load(); });
}

}  // namespace switch_auto::platform::switch_platform
