#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "switch_auto/application.hpp"
#include "switch_auto/companion.hpp"

int main() {
    using namespace switch_auto;

    CompanionSession session;
    const auto invalidPairing = session.begin("123");
    assert(!invalidPairing);
    assert(invalidPairing.error() == CompanionError::InvalidPairingCode);

    session.transportConnected();
    const auto pairRequest = session.begin("679922");
    assert(pairRequest);
    assert(pairRequest.value().find("\"type\":\"pair.request\"") != std::string::npos);
    assert(session.status().state == CompanionConnectionState::Pairing);

    const auto pairAccepted = session.receive(
        R"({"version":1,"type":"pair.accepted","sessionToken":"test-token"})");
    assert(pairAccepted);
    assert(pairAccepted.value().has_value());
    assert(pairAccepted.value()->find("\"type\":\"hello\"") != std::string::npos);
    assert(session.status().state == CompanionConnectionState::Handshaking);

    const auto helloAck = session.receive(
        R"({"version":1,"type":"hello.ack","server":"android-companion"})");
    assert(helloAck);
    assert(!helloAck.value().has_value());
    assert(session.status().state == CompanionConnectionState::Online);

    const auto ping = session.heartbeat();
    assert(ping);
    assert(ping.value().find("\"sequence\":1") != std::string::npos);
    const auto pong = session.receive(R"({"version":1,"type":"pong","sequence":1})");
    assert(pong);
    assert(session.status().lastSequence == 1);

    const auto secondPing = session.heartbeat();
    assert(secondPing);
    const auto mismatchedPong = session.receive(
        R"({"version":1,"type":"pong","sequence":99})");
    assert(!mismatchedPong);
    assert(mismatchedPong.error() == CompanionError::HeartbeatMismatch);
    assert(session.status().state == CompanionConnectionState::Failed);

    session.transportDisconnected(CompanionError::ConnectionClosed);
    assert(session.status().state == CompanionConnectionState::Reconnecting);
    assert(session.status().lastError == CompanionError::ConnectionClosed);

    std::array<std::uint32_t, 16> smallPixels{};
    Canvas smallCanvas(smallPixels.data(), 4, 4, 4);
    smallCanvas.clear({1, 2, 3, 255});
    assert(smallPixels[0] == 0xff030201U);
    assert(smallPixels[15] == 0xff030201U);

    smallCanvas.fillRect(-2, -2, 3, 3, {10, 20, 30, 255});
    assert(smallPixels[0] == 0xff1e140aU);
    assert(smallPixels[1] == 0xff030201U);

    static std::array<std::uint32_t, kLogicalWidth * kLogicalHeight> frame{};
    MutableRuntimeService runtime;
    Application application(runtime);
    assert(application.lifecycle() == ApplicationLifecycle::Created);
    assert(application.initialize());
    assert(application.lifecycle() == ApplicationLifecycle::Running);

    Canvas canvas(frame.data(), kLogicalWidth, kLogicalHeight, kLogicalWidth);
    application.update(1.0f / 60.0f);
    application.render(canvas);

    std::size_t nonBackgroundPixels = 0;
    for (const std::uint32_t pixel : frame) {
        if (pixel != 0xff18110dU) ++nonBackgroundPixels;
    }
    assert(nonBackgroundPixels > 10000);

    application.shutdown();
    assert(application.lifecycle() == ApplicationLifecycle::Stopped);
    application.shutdown();
    assert(application.lifecycle() == ApplicationLifecycle::Stopped);

    std::cout << "CORE_TESTS_OK\n";
    return 0;
}
