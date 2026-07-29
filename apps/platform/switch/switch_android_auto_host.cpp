#include "switch_android_auto_host.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

#include "switch_auto/android_auto_protocol.hpp"

namespace switch_auto::platform::switch_platform {
namespace {

constexpr auto kConnectTimeout = std::chrono::seconds(5);
constexpr auto kPollInterval = std::chrono::milliseconds(100);
constexpr auto kReconnectDelay = std::chrono::seconds(2);
constexpr auto kVersionTimeout = std::chrono::seconds(5);

bool sendAll(int socketFd, const std::vector<std::uint8_t>& data,
    const std::atomic<bool>& stopRequested) {
    std::size_t offset = 0;
    while (!stopRequested && offset < data.size()) {
        const ssize_t sent = ::send(socketFd, data.data() + offset,
            data.size() - offset, 0);
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) continue;
        return false;
    }
    return offset == data.size();
}

Result<AndroidAutoProtocolVersion, AndroidAutoError> negotiateVersion(
    int socketFd, const std::atomic<bool>& stopRequested) {
    const auto request = makeAndroidAutoVersionRequest();
    if (request.empty() || !sendAll(socketFd, request, stopRequested)) {
        return Result<AndroidAutoProtocolVersion, AndroidAutoError>::failure(
            AndroidAutoError::ConnectionClosed);
    }

    AndroidAutoFrameCodec codec;
    const auto deadline = std::chrono::steady_clock::now() + kVersionTimeout;
    while (!stopRequested && std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{socketFd, POLLIN | POLLHUP | POLLERR, 0};
        const int result = ::poll(
            &descriptor, 1, static_cast<int>(kPollInterval.count()));
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 ||
            (result > 0 &&
                (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)))) {
            return Result<AndroidAutoProtocolVersion,
                AndroidAutoError>::failure(AndroidAutoError::ConnectionClosed);
        }
        if (result <= 0 || !(descriptor.revents & POLLIN)) continue;

        std::uint8_t buffer[4096];
        const ssize_t received = ::recv(socketFd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return Result<AndroidAutoProtocolVersion,
                AndroidAutoError>::failure(AndroidAutoError::ConnectionClosed);
        }
        auto decoded = codec.feed(buffer, static_cast<std::size_t>(received));
        if (!decoded) {
            return Result<AndroidAutoProtocolVersion,
                AndroidAutoError>::failure(AndroidAutoError::ProtocolMismatch);
        }
        for (const auto& frame : decoded.value()) {
            auto version = parseAndroidAutoVersionResponse(frame);
            if (version) {
                return Result<AndroidAutoProtocolVersion,
                    AndroidAutoError>::success(version.value());
            }
            if (version.error() == AndroidAutoProtocolError::VersionRejected) {
                return Result<AndroidAutoProtocolVersion,
                    AndroidAutoError>::failure(
                    AndroidAutoError::NegotiationRejected);
            }
        }
    }
    return Result<AndroidAutoProtocolVersion, AndroidAutoError>::failure(
        AndroidAutoError::ConnectionTimeout);
}

int connectSocket(std::string_view address, std::uint16_t port,
    const std::atomic<bool>& stopRequested, AndroidAutoError& failure) {
    failure = AndroidAutoError::TransportUnavailable;
    const int socketFd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketFd < 0) return -1;

    const int originalFlags = ::fcntl(socketFd, F_GETFL, 0);
    if (originalFlags < 0 ||
        ::fcntl(socketFd, F_SETFL, originalFlags | O_NONBLOCK) < 0) {
        ::close(socketFd);
        return -1;
    }

    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    const std::string addressText(address);
    if (::inet_pton(AF_INET, addressText.c_str(), &endpoint.sin_addr) != 1) {
        ::close(socketFd);
        failure = AndroidAutoError::DiscoveryTimeout;
        return -1;
    }

    if (::connect(socketFd, reinterpret_cast<sockaddr*>(&endpoint),
            sizeof(endpoint)) < 0 && errno != EINPROGRESS) {
        ::close(socketFd);
        return -1;
    }

    const auto deadline = std::chrono::steady_clock::now() + kConnectTimeout;
    bool connected = false;
    while (!stopRequested && std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{socketFd, POLLOUT, 0};
        const int result = ::poll(
            &descriptor, 1, static_cast<int>(kPollInterval.count()));
        if (result < 0 && errno != EINTR) break;
        if (result <= 0) continue;
        int socketError = 0;
        socklen_t errorLength = sizeof(socketError);
        if (::getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &socketError,
                &errorLength) == 0 && socketError == 0) {
            connected = true;
        }
        break;
    }

    if (!connected && !stopRequested &&
        std::chrono::steady_clock::now() >= deadline) {
        failure = AndroidAutoError::ConnectionTimeout;
    }
    if (!connected || stopRequested ||
        ::fcntl(socketFd, F_SETFL, originalFlags) < 0) {
        ::close(socketFd);
        return -1;
    }
    return socketFd;
}

}  // namespace

SwitchAndroidAutoHost::SwitchAndroidAutoHost(
    EventQueue& events, EndpointProvider endpointProvider)
    : events_(events), endpointProvider_(std::move(endpointProvider)) {}

SwitchAndroidAutoHost::~SwitchAndroidAutoHost() {
    stop();
}

Result<void, AndroidAutoError> SwitchAndroidAutoHost::start() {
    if (worker_.joinable()) {
        return Result<void, AndroidAutoError>::success();
    }
    stopRequested_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.reset();
        const auto result = session_.startDiscovery();
        if (!result) return result;
    }
    publish();
    worker_ = std::thread(&SwitchAndroidAutoHost::run, this);
    return Result<void, AndroidAutoError>::success();
}

void SwitchAndroidAutoHost::suspend() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.suspend();
    }
    publish();
}

void SwitchAndroidAutoHost::resume() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.resume();
    }
    publish();
}

void SwitchAndroidAutoHost::stop() {
    stopRequested_ = true;
    disconnectSocket();
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.reset();
    }
    publish();
}

AndroidAutoHostStatus SwitchAndroidAutoHost::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_.status();
}

Result<void, AndroidAutoError> SwitchAndroidAutoHost::sendTouch(
    const AndroidAutoTouchEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto validation = session_.validateTouch(event);
    if (!validation) return validation;
    // The input channel is enabled only after protocol negotiation is added.
    return Result<void, AndroidAutoError>::failure(
        AndroidAutoError::InputUnavailable);
}

void SwitchAndroidAutoHost::setStatusHandler(StatusHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    statusHandler_ = std::move(handler);
}

void SwitchAndroidAutoHost::setVideoHandler(VideoHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    videoHandler_ = std::move(handler);
}

void SwitchAndroidAutoHost::setAudioHandler(AudioHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    audioHandler_ = std::move(handler);
}

void SwitchAndroidAutoHost::run() {
    while (!stopRequested_) {
        const Endpoint endpoint =
            endpointProvider_ ? endpointProvider_() : Endpoint{};
        if (endpoint.address.empty() || endpoint.port == 0) {
            std::this_thread::sleep_for(kReconnectDelay);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (session_.status().state == AndroidAutoHostState::Reconnecting ||
                session_.status().state == AndroidAutoHostState::Failed) {
                session_.reset();
                session_.startDiscovery();
            }
            const auto result = session_.beginConnection(
                endpoint.address + ":" + std::to_string(endpoint.port));
            if (!result) {
                session_.fail(result.error());
            }
        }
        publish();

        AndroidAutoError failure = AndroidAutoError::TransportUnavailable;
        const int socketFd = connectSocket(
            endpoint.address, endpoint.port, stopRequested_, failure);
        if (socketFd < 0) {
            if (!stopRequested_) {
                std::lock_guard<std::mutex> lock(mutex_);
                session_.transportDisconnected(failure);
            }
            publish();
            std::this_thread::sleep_for(kReconnectDelay);
            continue;
        }

        activeSocket_ = socketFd;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session_.transportConnected();
        }
        publish();

        const auto version = negotiateVersion(socketFd, stopRequested_);
        if (!version) {
            disconnectSocket();
            if (!stopRequested_) {
                std::lock_guard<std::mutex> lock(mutex_);
                session_.transportDisconnected(version.error());
            }
            publish();
            std::this_thread::sleep_for(kReconnectDelay);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto result = session_.versionNegotiated(version.value());
            if (!result) session_.fail(result.error());
        }
        publish();

        // Version negotiation is complete. TLS handshake and service discovery
        // are the next protocol stages; until then, monitor the TCP connection.
        while (!stopRequested_) {
            pollfd descriptor{socketFd, POLLIN | POLLHUP | POLLERR, 0};
            const int result = ::poll(
                &descriptor, 1, static_cast<int>(kPollInterval.count()));
            if (result < 0 && errno != EINTR) break;
            if (result > 0 &&
                (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL))) break;
            if (result > 0 && (descriptor.revents & POLLIN)) {
                std::uint8_t probe = 0;
                const ssize_t received = ::recv(socketFd, &probe, 1, MSG_PEEK);
                if (received <= 0) break;
            }
        }
        disconnectSocket();
        if (!stopRequested_) {
            std::lock_guard<std::mutex> lock(mutex_);
            session_.transportDisconnected(AndroidAutoError::ConnectionClosed);
        }
        publish();
        std::this_thread::sleep_for(kReconnectDelay);
    }
}

void SwitchAndroidAutoHost::publish() {
    AndroidAutoHostStatus current;
    StatusHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current = session_.status();
        handler = statusHandler_;
    }
    if (!handler) return;
    events_.post([handler = std::move(handler), current]() {
        handler(current);
    });
}

void SwitchAndroidAutoHost::disconnectSocket() {
    const int socketFd = activeSocket_.exchange(-1);
    if (socketFd >= 0) {
        ::shutdown(socketFd, SHUT_RDWR);
        ::close(socketFd);
    }
}

}  // namespace switch_auto::platform::switch_platform
