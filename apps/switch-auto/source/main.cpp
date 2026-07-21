#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <switch.h>

namespace {

constexpr uint16_t kCompanionPort = 38471;
constexpr int kProtocolVersion = 1;
constexpr size_t kBufferSize = 1024;

bool promptText(const char* header, const char* guide, const char* initial,
                char* output, size_t outputSize) {
    SwkbdConfig keyboard;
    Result result = swkbdCreate(&keyboard, 0);
    if (R_FAILED(result)) {
        printf("Keyboard creation failed: 0x%x\n", result);
        return false;
    }

    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, header);
    swkbdConfigSetGuideText(&keyboard, guide);
    swkbdConfigSetInitialText(&keyboard, initial);
    result = swkbdShow(&keyboard, output, outputSize);
    swkbdClose(&keyboard);
    return R_SUCCEEDED(result) && output[0] != '\0';
}

bool sendAll(int socketFd, const char* text) {
    size_t remaining = strlen(text);
    const char* cursor = text;
    while (remaining > 0) {
        const ssize_t sent = send(socketFd, cursor, remaining, 0);
        if (sent <= 0) {
            return false;
        }
        cursor += sent;
        remaining -= static_cast<size_t>(sent);
    }
    return true;
}

bool receiveLine(int socketFd, char* output, size_t outputSize) {
    size_t used = 0;
    while (used + 1 < outputSize) {
        char value = '\0';
        const ssize_t received = recv(socketFd, &value, 1, 0);
        if (received == 0) {
            return false;
        }
        if (received < 0) {
            return false;
        }
        if (value == '\n') {
            output[used] = '\0';
            return true;
        }
        output[used++] = value;
    }
    output[outputSize - 1] = '\0';
    return false;
}

bool containsType(const char* json, const char* type) {
    char expected[96];
    snprintf(expected, sizeof(expected), "\"type\":\"%s\"", type);
    return strstr(json, expected) != nullptr;
}

int connectToCompanion(const char* ipAddress) {
    const int socketFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketFd < 0) {
        printf("socket() failed: %d\n", errno);
        return -1;
    }

    timeval timeout{};
    timeout.tv_sec = 15;
    setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kCompanionPort);
    if (inet_pton(AF_INET, ipAddress, &address.sin_addr) != 1) {
        printf("Invalid IPv4 address.\n");
        close(socketFd);
        return -1;
    }

    printf("Connecting to %s:%u...\n", ipAddress, kCompanionPort);
    consoleUpdate(nullptr);
    if (connect(socketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        printf("connect() failed: %d\n", errno);
        close(socketFd);
        return -1;
    }
    return socketFd;
}

bool performHandshake(int socketFd, const char* code) {
    char request[256];
    snprintf(request, sizeof(request),
             "{\"version\":%d,\"type\":\"pair.request\","
             "\"deviceName\":\"Nintendo Switch\",\"code\":\"%s\"}\n",
             kProtocolVersion, code);

    if (!sendAll(socketFd, request)) {
        printf("Failed to send pair request.\n");
        return false;
    }

    char response[kBufferSize];
    if (!receiveLine(socketFd, response, sizeof(response))) {
        printf("No pair response.\n");
        return false;
    }
    printf("RX: %s\n", response);
    if (!containsType(response, "pair.accepted")) {
        printf("PAIR REJECTED\n");
        return false;
    }
    printf("PAIR ACCEPTED\n");

    const char* hello =
        "{\"version\":1,\"type\":\"hello\",\"client\":\"switch-auto\"}\n";
    if (!sendAll(socketFd, hello) || !receiveLine(socketFd, response, sizeof(response))) {
        printf("Hello exchange failed.\n");
        return false;
    }
    printf("RX: %s\n", response);
    if (!containsType(response, "hello.ack")) {
        printf("Unexpected hello response.\n");
        return false;
    }
    printf("HELLO ACK - connection validated.\n");
    return true;
}

void runHeartbeat(int socketFd, PadState* pad) {
    int sequence = 1;
    while (appletMainLoop()) {
        padUpdate(pad);
        if (padGetButtonsDown(pad) & HidNpadButton_Plus) {
            break;
        }

        char ping[128];
        snprintf(ping, sizeof(ping),
                 "{\"version\":1,\"type\":\"ping\",\"sequence\":%d}\n",
                 sequence);
        if (!sendAll(socketFd, ping)) {
            printf("Connection lost while sending ping.\n");
            break;
        }

        char response[kBufferSize];
        if (!receiveLine(socketFd, response, sizeof(response))) {
            printf("Connection lost while waiting for pong.\n");
            break;
        }
        printf("PONG %d: %s\n", sequence++, response);
        consoleUpdate(nullptr);

        for (int i = 0; i < 300 && appletMainLoop(); ++i) {
            padUpdate(pad);
            if (padGetButtonsDown(pad) & HidNpadButton_Plus) {
                return;
            }
            svcSleepThread(16'666'667L);
        }
    }
}

}  // namespace

int main(int, char**) {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    printf("Switch Auto - handshake prototype v0.1.0\n");
    printf("TCP/JSON protocol v1 - port %u\n\n", kCompanionPort);
    consoleUpdate(nullptr);

    if (R_FAILED(socketInitializeDefault())) {
        printf("Network initialization failed.\n");
        consoleUpdate(nullptr);
    } else {
        char ipAddress[64] = "192.168.43.1";
        char pairingCode[16] = {};
        if (promptText("Android Companion IP", "Example: 192.168.43.1",
                       ipAddress, ipAddress, sizeof(ipAddress)) &&
            promptText("Pairing code", "Enter the 6-digit code", "",
                       pairingCode, sizeof(pairingCode))) {
            const int socketFd = connectToCompanion(ipAddress);
            if (socketFd >= 0) {
                if (performHandshake(socketFd, pairingCode)) {
                    printf("\nHeartbeat active. Press + to exit.\n");
                    consoleUpdate(nullptr);
                    runHeartbeat(socketFd, &pad);
                }
                close(socketFd);
            }
        }
        socketExit();
    }

    printf("\nPress + to return to hbmenu.\n");
    consoleUpdate(nullptr);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
    return 0;
}
