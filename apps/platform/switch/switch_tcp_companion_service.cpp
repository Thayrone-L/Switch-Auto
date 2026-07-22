#include "switch_tcp_companion_service.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace switch_auto::platform::switch_platform {
namespace {

constexpr int kSocketTimeoutSeconds = 10;
constexpr auto kReconnectDelay = std::chrono::seconds(3);
constexpr auto kHeartbeatInterval = std::chrono::seconds(5);
constexpr std::size_t kMaximumMessageSize = 4096;

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

}  // namespace

SwitchTcpCompanionService::~SwitchTcpCompanionService() {
    disconnect();
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
    publish({CompanionConnectionState::Connecting, CompanionError::None});
    worker_ = std::thread(
        &SwitchTcpCompanionService::run, this, endpoint, std::string(pairingCode));
    return Result<void, CompanionError>::success();
}

void SwitchTcpCompanionService::disconnect() {
    stopRequested_ = true;
    waitCondition_.notify_all();
    if (worker_.joinable()) worker_.join();
    publish({});
}

CompanionStatus SwitchTcpCompanionService::status() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}

void SwitchTcpCompanionService::run(
    CompanionEndpoint endpoint, std::string pairingCode) {
    while (!stopRequested_) {
        CompanionSession session;
        session.transportConnected();
        publish(session.status());

        const int socketFd = openSocket(endpoint);
        if (socketFd < 0) {
            session.transportDisconnected(CompanionError::TransportUnavailable);
            publish(session.status());
            if (!waitForRetry()) break;
            continue;
        }

        CompanionError failure = CompanionError::None;
        const auto pairRequest = session.begin(pairingCode);
        publish(session.status());
        std::string response;

        if (!pairRequest || !sendAll(socketFd, pairRequest.value()) ||
            !receiveLine(socketFd, response)) {
            failure = pairRequest ? CompanionError::ConnectionClosed : pairRequest.error();
        } else {
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
            }
        }

        while (failure == CompanionError::None && !stopRequested_) {
            std::unique_lock<std::mutex> lock(waitMutex_);
            if (waitCondition_.wait_for(lock, kHeartbeatInterval,
                    [this] { return stopRequested_.load(); })) break;
            lock.unlock();

            const auto ping = session.heartbeat();
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
        if (stopRequested_) break;
        if (failure != CompanionError::None && permanentProtocolError(failure)) {
            publish(session.status());
            break;
        }
        session.transportDisconnected(
            failure == CompanionError::None ? CompanionError::ConnectionClosed : failure);
        publish(session.status());
        if (!waitForRetry()) break;
    }
}

void SwitchTcpCompanionService::publish(const CompanionStatus& status) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_ = status;
}

bool SwitchTcpCompanionService::waitForRetry() {
    std::unique_lock<std::mutex> lock(waitMutex_);
    return !waitCondition_.wait_for(lock, kReconnectDelay,
        [this] { return stopRequested_.load(); });
}

}  // namespace switch_auto::platform::switch_platform
