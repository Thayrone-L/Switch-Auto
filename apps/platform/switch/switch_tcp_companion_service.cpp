#include "switch_tcp_companion_service.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <borealis.hpp>
#include <switch.h>

namespace switch_auto::platform::switch_platform {
namespace {

constexpr int kSocketTimeoutSeconds = 10;
constexpr std::uint16_t kDiscoveryPort = 38472;
constexpr int kDiscoveryAttempts = 4;
constexpr int kDiscoveryTimeoutSeconds = 1;
constexpr auto kMinimumReconnectDelay = std::chrono::seconds(1);
constexpr auto kMaximumReconnectDelay = std::chrono::seconds(16);
constexpr auto kHeartbeatInterval = std::chrono::seconds(5);
constexpr std::size_t kMaximumMessageSize = 4096;
constexpr const char* kTrustedCompanionPath =
    "sdmc:/switch/switch-auto/identity/companion.trusted";

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

int openSocket(const CompanionEndpoint& endpoint) {
    const int socketFd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketFd < 0) return -1;

    timeval timeout{};
    timeout.tv_sec = kSocketTimeoutSeconds;
    ::setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    if (::inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr) != 1 ||
        ::connect(socketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(socketFd);
        return -1;
    }
    return socketFd;
}

bool permanentProtocolError(CompanionError error) {
    return error == CompanionError::InvalidPairingCode ||
           error == CompanionError::PairingRejected ||
           error == CompanionError::UnsupportedVersion ||
           error == CompanionError::InvalidMessage;
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
        "{\"version\":1,\"type\":\"discover.request\",\"deviceId\":\"" +
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
            std::strstr(buffer, "\"version\":1")) {
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

SwitchTcpCompanionService::SwitchTcpCompanionService() {
    auto identity = SwitchDeviceIdentity::loadOrCreate();
    if (identity) {
        identity_ = std::move(identity.value());
        if (std::FILE* trusted = std::fopen(kTrustedCompanionPath, "rb")) {
            hasTrustedCompanion_ = true;
            std::fclose(trusted);
        }
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
    disconnect();
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
    if (!validator.begin(pairingCode)) {
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
    stopRequested_ = true;
    waitCondition_.notify_all();
    if (worker_.joinable()) worker_.join();
    brls::Logger::info("Companion transport: disconnected");
    publish({});
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
        reconnecting.retryDelaySeconds = static_cast<std::uint32_t>(delay.count());
        publish(reconnecting);
        brls::Logger::info("Companion reconnect: attempt={} delay={}s reason={}",
            retryAttempt, delay.count(), toString(failure));
        if (!waitForRetry(delay)) return;
        delay = std::min(delay * 2, kMaximumReconnectDelay);
    }
}

void SwitchTcpCompanionService::runManual(
    CompanionEndpoint endpoint, std::string pairingToken) {
    auto delay = kMinimumReconnectDelay;
    while (!stopRequested_) {
        const CompanionError failure = runSession(endpoint, pairingToken);
        if (stopRequested_ || permanentProtocolError(failure)) return;
        if (!waitForRetry(delay)) return;
        delay = std::min(delay * 2, kMaximumReconnectDelay);
    }
}

CompanionError SwitchTcpCompanionService::runSession(
    const CompanionEndpoint& endpoint, const std::string& pairingToken) {
    CompanionSession session;
    session.transportConnected();
    CompanionStatus connecting = session.status();
    connecting.endpointAddress = endpoint.address;
    publish(connecting);

    const int socketFd = openSocket(endpoint);
    if (socketFd < 0) {
        brls::Logger::error("Companion TCP: connection to {}:{} failed errno={}",
            endpoint.address, endpoint.port, errno);
        return CompanionError::TransportUnavailable;
    }
    brls::Logger::info("Companion TCP: connected to {}:{}", endpoint.address,
        endpoint.port);

    CompanionError failure = CompanionError::None;
    const auto pairRequest = session.begin(
        pairingToken, identity_ ? identity_->pairingInfo().deviceId : "");
    publish(session.status());
    std::string response;

    if (!pairRequest || !sendAll(socketFd, pairRequest.value()) ||
            !receiveLine(socketFd, response)) {
            failure = pairRequest ? CompanionError::ConnectionClosed : pairRequest.error();
        } else {
            brls::Logger::info("Companion protocol: pair.request sent deviceId={}",
                identity_ ? identity_->pairingInfo().deviceId : "unknown");
            const auto pairResponse = session.receive(response);
            publish(session.status());
            if (!pairResponse) {
                failure = pairResponse.error();
            } else if (!pairResponse.value() ||
                       !sendAll(socketFd, *pairResponse.value()) ||
                       !receiveLine(socketFd, response)) {
                failure = CompanionError::ConnectionClosed;
            } else {
                const auto helloResponse = session.receive(response);
                publish(session.status());
                if (!helloResponse) failure = helloResponse.error();
                else {
                    brls::Logger::info("Companion protocol: session online");
                    hasTrustedCompanion_ = true;
                    if (std::FILE* trusted = std::fopen(kTrustedCompanionPath, "wb")) {
                        std::fputs("1\n", trusted);
                        std::fclose(trusted);
                    }
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
            const auto ping = session.heartbeat(localBattery);
            if (!ping || !sendAll(socketFd, ping.value()) ||
                !receiveLine(socketFd, response)) {
                failure = CompanionError::ConnectionClosed;
                break;
            }
            const auto pong = session.receive(response);
            publish(session.status());
            if (!pong) failure = pong.error();
        }

    ::shutdown(socketFd, SHUT_RDWR);
    ::close(socketFd);
    if (stopRequested_) return CompanionError::None;
    if (failure == CompanionError::None) failure = CompanionError::ConnectionClosed;
    session.transportDisconnected(failure);
    brls::Logger::error("Companion transport: session ended ({})", toString(failure));
    publish(session.status());
    return failure;
}

void SwitchTcpCompanionService::publish(const CompanionStatus& status) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_ = status;
}

bool SwitchTcpCompanionService::waitForRetry(std::chrono::seconds delay) {
    std::unique_lock<std::mutex> lock(waitMutex_);
    return !waitCondition_.wait_for(lock, delay,
        [this] { return stopRequested_.load(); });
}

}  // namespace switch_auto::platform::switch_platform
