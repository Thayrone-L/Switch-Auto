#include <switch.h>

#include <cstdio>

int main() {
    consoleInit(nullptr);

    PadState pad{};
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    std::printf("Switch Auto V3\n");
    std::printf("Foundation bootstrap\n\n");
    std::printf("Press + to close.\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0) {
            break;
        }
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}
