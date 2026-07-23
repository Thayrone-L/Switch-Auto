#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "switch_auto/application.hpp"
#include "switch_auto/companion.hpp"
#include "switch_auto/event_queue.hpp"
#include "switch_auto/localization.hpp"
#include "switch_auto/permissions.hpp"
#include "switch_auto/crypto.hpp"
#include "switch_auto/service_registry.hpp"
#include "switch_auto/widget.hpp"
#include "switch_auto/widget_layout.hpp"

int main() {
    using namespace switch_auto;

    CompanionSession session;
    const auto invalidPairing = session.begin("123", "TEST", "0123456789abcdef0123456789abcdef");
    assert(!invalidPairing);
    assert(invalidPairing.error() == CompanionError::InvalidPairingCode);

    session.transportConnected();
    const std::string pairingToken =
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
    const std::string clientNonce = "0123456789abcdef0123456789abcdef";
    const std::string serverNonce = "fedcba9876543210fedcba9876543210";
    const auto pairRequest = session.begin(
        pairingToken, "TESTDEVICE", clientNonce, "abcdef123456");
    assert(pairRequest);
    assert(pairRequest.value().find("\"type\":\"pair.request\"") != std::string::npos);
    assert(pairRequest.value().find("\"sessionId\":\"abcdef123456\"") !=
        std::string::npos);
    assert(pairRequest.value().find("pairingToken") == std::string::npos);
    assert(session.status().state == CompanionConnectionState::Pairing);

    const std::string serverProof = crypto::hmacSha256Hex(pairingToken,
        "server|TESTDEVICE|" + clientNonce + "|" + serverNonce);
    const auto challenge = session.receive(
        "{\"version\":3,\"type\":\"pair.challenge\",\"serverNonce\":\"" +
        serverNonce + "\",\"proof\":\"" + serverProof + "\"}");
    assert(challenge);
    assert(challenge.value().has_value());
    assert(challenge.value()->find("\"type\":\"pair.proof\"") != std::string::npos);
    assert(session.status().state == CompanionConnectionState::Authenticating);

    const auto pairAccepted = session.receive(
        R"({"version":3,"type":"pair.accepted","sessionToken":"test-token"})");
    assert(pairAccepted);
    assert(pairAccepted.value().has_value());
    assert(pairAccepted.value()->find("\"type\":\"hello\"") != std::string::npos);
    assert(session.status().state == CompanionConnectionState::Handshaking);

    CompanionSession tamperedSession;
    assert(tamperedSession.begin(pairingToken, "TESTDEVICE", clientNonce));
    const auto tampered = tamperedSession.receive(
        "{\"version\":3,\"type\":\"pair.challenge\",\"serverNonce\":\"" +
        serverNonce + "\",\"proof\":\"" + std::string(64, '0') + "\"}");
    assert(!tampered);
    assert(tampered.error() == CompanionError::AuthenticationFailed);

    CompanionSession replaySession;
    const std::string freshClientNonce = "11111111111111111111111111111111";
    assert(replaySession.begin(pairingToken, "TESTDEVICE", freshClientNonce));
    const auto replayed = replaySession.receive(
        "{\"version\":3,\"type\":\"pair.challenge\",\"serverNonce\":\"" +
        serverNonce + "\",\"proof\":\"" + serverProof + "\"}");
    assert(!replayed);
    assert(replayed.error() == CompanionError::AuthenticationFailed);

    assert(crypto::hmacSha256Hex("6b6579", "The quick brown fox jumps over the lazy dog") ==
        "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");

    const auto helloAck = session.receive(
        R"({"version":3,"type":"hello.ack","server":"android-companion"})");
    assert(helloAck);
    assert(!helloAck.value().has_value());
    assert(session.status().state == CompanionConnectionState::Online);

    const auto ping = session.heartbeat(64);
    assert(ping);
    assert(ping.value().find("\"sequence\":1") != std::string::npos);
    assert(ping.value().find("\"switchBatteryPercent\":64") != std::string::npos);
    const auto pong = session.receive(
        R"({"version":3,"type":"pong","sequence":1,"phoneBatteryPercent":73})");
    assert(pong);
    assert(session.status().lastSequence == 1);
    assert(session.status().remoteBatteryPercent == 73);

    const auto locationPing = session.heartbeat();
    assert(locationPing);
    const auto locationPong = session.receive(
        R"({"version":3,"type":"pong","sequence":2,"phoneBatteryPercent":73,"location":{"latitude":-23.55052,"longitude":-46.63331,"accuracyMeters":4.5,"speedMetersPerSecond":13.9,"bearingDegrees":180.0,"timestamp":1780000000000}})");
    assert(locationPong);
    assert(session.status().remoteLocation.has_value());
    assert(session.status().remoteLocation->latitude == -23.55052);
    assert(session.status().remoteLocation->speedMetersPerSecond == 13.9);

    const auto mapPing = session.heartbeat();
    assert(mapPing);
    const auto mapPong = session.receive(
        R"({"version":3,"type":"pong","sequence":3,"location":{"latitude":-23.55052,"longitude":-46.63331,"accuracyMeters":4.5},"mapFrame":{"id":7,"contentType":"image/jpeg","length":4,"width":640,"height":360}})");
    assert(mapPong);
    assert(session.pendingBinaryLength() == 4);
    assert(session.acceptBinaryPayload({0xff, 0xd8, 0xff, 0xd9}));
    assert(session.status().remoteMapFrame.has_value());
    assert(session.status().remoteMapFrame->id == 7);
    assert(session.status().remoteMapFrame->data.size() == 4);

    const auto degradedMapPing = session.heartbeat();
    assert(degradedMapPing);
    const auto degradedMapPong = session.receive(
        R"({"version":3,"type":"pong","sequence":4,"mapFrame":{"id":8,"contentType":"unexpected","length":4,"width":0,"height":0},"customization":{"schemaVersion":99}})");
    assert(degradedMapPong);
    assert(session.pendingBinaryLength() == 4);
    assert(session.acceptBinaryPayload({0xff, 0xd8, 0xff, 0xd9}));
    assert(session.status().remoteMapFrame.has_value());
    assert(session.status().remoteMapFrame->width == 640);
    assert(!session.status().remoteCustomization.has_value());

    const auto customizationPing = session.heartbeat(std::nullopt, 6);
    assert(customizationPing);
    assert(customizationPing.value().find("\"customizationRevision\":6") !=
        std::string::npos);
    const auto customizationPong = session.receive(
        R"({"version":3,"type":"pong","sequence":5,"customization":{"schemaVersion":1,"revision":7,"themeId":"default-dark","layoutId":"automotive","layout":{"schemaVersion":1,"widgets":[{"id":"core.gps","column":0,"row":0,"columnSpan":8,"rowSpan":8,"minColumns":5,"minRows":4,"visible":1},{"id":"core.speed","column":8,"row":0,"columnSpan":4,"rowSpan":2,"minColumns":2,"minRows":2,"visible":1},{"id":"core.media","column":8,"row":2,"columnSpan":4,"rowSpan":6,"minColumns":3,"minRows":3,"visible":1}]}}})");
    assert(customizationPong);
    assert(session.status().remoteCustomization.has_value());
    assert(session.status().remoteCustomization->revision == 7);
    assert(session.status().remoteCustomization->themeId == "default-dark");
    assert(WidgetLayout::deserialize(
        session.status().remoteCustomization->layoutJson).has_value());

    const auto secondPing = session.heartbeat();
    assert(secondPing);
    const auto mismatchedPong = session.receive(
        R"({"version":3,"type":"pong","sequence":99})");
    assert(!mismatchedPong);
    assert(mismatchedPong.error() == CompanionError::HeartbeatMismatch);
    assert(session.status().state == CompanionConnectionState::Failed);

    session.transportDisconnected(CompanionError::ConnectionClosed);
    assert(session.status().state == CompanionConnectionState::Reconnecting);
    assert(session.status().lastError == CompanionError::ConnectionClosed);

    // Transport drop is recoverable and preserves a precise failure reason.
    CompanionSession droppedSession;
    droppedSession.transportConnected();
    droppedSession.transportDisconnected(CompanionError::ConnectionTimeout);
    assert(droppedSession.status().state == CompanionConnectionState::Reconnecting);
    assert(droppedSession.status().lastError == CompanionError::ConnectionTimeout);

    // A server-side pairing rejection is permanent for the current attempt.
    CompanionSession rejectedSession;
    assert(rejectedSession.begin(pairingToken, "TESTDEVICE", clientNonce));
    const auto rejected = rejectedSession.receive(
        R"({"version":3,"type":"pair.rejected","reason":"unknown_device"})");
    assert(!rejected);
    assert(rejected.error() == CompanionError::PairingRejected);
    assert(rejectedSession.status().state == CompanionConnectionState::Failed);

    // A new transport attempt can leave reconnecting and restart the handshake.
    droppedSession.transportConnected();
    assert(droppedSession.status().state == CompanionConnectionState::Connecting);
    assert(droppedSession.begin(pairingToken, "TESTDEVICE", clientNonce));
    assert(droppedSession.status().state == CompanionConnectionState::Pairing);

    // Protocol versions fail closed before any message-specific processing.
    CompanionSession incompatibleSession;
    assert(incompatibleSession.begin(pairingToken, "TESTDEVICE", clientNonce));
    const auto incompatible = incompatibleSession.receive(
        R"({"version":99,"type":"pair.challenge"})");
    assert(!incompatible);
    assert(incompatible.error() == CompanionError::UnsupportedVersion);
    assert(incompatibleSession.status().state == CompanionConnectionState::Failed);

    std::array<std::uint32_t, 16> smallPixels{};
    Canvas smallCanvas(smallPixels.data(), 4, 4, 4);
    smallCanvas.clear({1, 2, 3, 255});
    assert(smallPixels[0] == 0xff030201U);
    assert(smallPixels[15] == 0xff030201U);

    smallCanvas.fillRect(-2, -2, 3, 3, {10, 20, 30, 255});
    assert(smallPixels[0] == 0xff1e140aU);
    assert(smallPixels[1] == 0xff030201U);
    smallCanvas.fillRect(10, 10, 2, 2, {255, 0, 0, 255});
    assert(smallPixels[15] == 0xff030201U);

    static std::array<std::uint32_t, kLogicalWidth * kLogicalHeight> frame{};
    MutableRuntimeService runtime;
    CatalogLocalizationService localization;
    assert(localization.addCatalog("en-US", {
        {"status.connected", "CONNECTED"},
        {"status.offline", "OFFLINE"},
    }));
    assert(localization.addCatalog("pt-BR", {
        {"status.connected", "CONECTADO"},
    }));
    assert(!localization.addCatalog("", {{"key", "value"}}));
    assert(!localization.addCatalog("invalid", {{"", "value"}}));
    assert(localization.setLocale("pt-BR"));
    assert(localization.translate("status.connected") == "CONECTADO");
    assert(localization.translate("status.offline") == "OFFLINE");
    assert(localization.translate("missing.key") == "missing.key");
    assert(!localization.setLocale("es-ES"));
    assert(localization.locale() == "pt-BR");

    MutablePermissionService permissions;
    assert(permissions.state(Capability::Location) == PermissionState::Unknown);
    assert(permissions.canRequest(Capability::Location));
    permissions.setState(Capability::Location, PermissionState::Denied);
    PermissionState requestedState = PermissionState::Unknown;
    permissions.request(Capability::Location,
        [&requestedState](PermissionState state) { requestedState = state; });
    assert(requestedState == PermissionState::Denied);
    permissions.setState(
        Capability::Location, PermissionState::PermanentlyDenied);
    assert(!permissions.canRequest(Capability::Location));
    permissions.setState(
        Capability::MediaControl, PermissionState::RequiresSettings);
    assert(permissions.canRequest(Capability::MediaControl));
    assert(permissionDescriptor(Capability::AnswerCalls).sensitive);
    assert(std::string(toString(Capability::BackgroundExecution)) ==
        "background_execution");

    ServiceRegistry services;
    assert(services.registerService<RuntimeService>(runtime));
    assert(services.registerService<LocalizationService>(localization));
    assert(!services.registerService<RuntimeService>(runtime));
    assert(&services.require<RuntimeService>() == &runtime);

    EventQueue events;
    int eventValue = 0;
    events.post([&eventValue]() { eventValue = 42; });
    assert(events.drain() == 1);
    assert(eventValue == 42);

    WidgetRegistry widgetRegistry;
    assert(widgetRegistry.registerWidget(makeBatteryWidget()));
    assert(!widgetRegistry.registerWidget(makeBatteryWidget()));
    assert(widgetRegistry.initializeAll({runtime, services}));
    assert(widgetRegistry.lifecycle("core.battery") == WidgetLifecycle::Active);
    assert(widgetRegistry.suspend("core.battery"));
    assert(widgetRegistry.lifecycle("core.battery") == WidgetLifecycle::Suspended);
    assert(widgetRegistry.resume("core.battery"));
    widgetRegistry.disposeAll();
    assert(widgetRegistry.lifecycle("core.battery") == WidgetLifecycle::Disposed);

    WidgetLayout layout = WidgetLayout::defaultAutomotive();
    assert(layout.validate() == WidgetLayoutError::None);
    assert(layout.find("core.gps")->columnSpan == 8);
    assert(layout.resize("core.gps", 4, 4) == WidgetLayoutError::BelowMinimumSize);
    assert(layout.move("core.speed", 7, 0) == WidgetLayoutError::Overlap);
    assert(layout.move("core.speed", 8, 6) == WidgetLayoutError::Overlap);
    assert(layout.setVisible("core.media", false) == WidgetLayoutError::None);
    assert(layout.move("core.speed", 8, 6) == WidgetLayoutError::None);
    const std::string serializedLayout = layout.serialize();
    const auto restoredLayout = WidgetLayout::deserialize(serializedLayout);
    assert(restoredLayout.has_value());
    assert(restoredLayout->find("core.speed")->row == 6);
    assert(!WidgetLayout::deserialize(
        R"({"schemaVersion":99,"widgets":[]})").has_value());

    Application application(runtime, localization);
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
