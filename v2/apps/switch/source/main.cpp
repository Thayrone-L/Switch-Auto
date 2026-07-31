#include <deko3d.h>
#include <switch.h>

#include <cstdio>

#include "switch_auto_v2/platform/nvtegra_probe.hpp"

namespace {

constexpr unsigned kFramebufferCount = 2;
constexpr unsigned kWidth = 1280;
constexpr unsigned kHeight = 720;
constexpr unsigned kCommandMemorySize = 16 * 1024;

struct Renderer {
    DkDevice device{};
    DkMemBlock framebufferMemory{};
    DkImage framebuffers[kFramebufferCount]{};
    DkSwapchain swapchain{};
    DkMemBlock commandMemory{};
    DkCmdBuf commandBuffer{};
    DkCmdList bindFramebuffer[kFramebufferCount]{};
    DkCmdList clearScreen{};
    DkQueue queue{};

    bool initialize() {
        DkDeviceMaker deviceMaker;
        dkDeviceMakerDefaults(&deviceMaker);
        device = dkDeviceCreate(&deviceMaker);
        if (!device) return false;

        DkImageLayoutMaker layoutMaker;
        dkImageLayoutMakerDefaults(&layoutMaker, device);
        layoutMaker.flags = DkImageFlags_UsageRender |
            DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
        layoutMaker.format = DkImageFormat_RGBA8_Unorm;
        layoutMaker.dimensions[0] = kWidth;
        layoutMaker.dimensions[1] = kHeight;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &layoutMaker);

        const unsigned alignment = dkImageLayoutGetAlignment(&layout);
        const unsigned imageSize =
            (dkImageLayoutGetSize(&layout) + alignment - 1) & ~(alignment - 1);
        DkMemBlockMaker memoryMaker;
        dkMemBlockMakerDefaults(
            &memoryMaker, device, kFramebufferCount * imageSize);
        memoryMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
        framebufferMemory = dkMemBlockCreate(&memoryMaker);
        if (!framebufferMemory) return false;

        const DkImage* swapchainImages[kFramebufferCount]{};
        for (unsigned index = 0; index < kFramebufferCount; ++index) {
            dkImageInitialize(&framebuffers[index], &layout,
                framebufferMemory, index * imageSize);
            swapchainImages[index] = &framebuffers[index];
        }
        DkSwapchainMaker swapchainMaker;
        dkSwapchainMakerDefaults(&swapchainMaker, device,
            nwindowGetDefault(), swapchainImages, kFramebufferCount);
        swapchain = dkSwapchainCreate(&swapchainMaker);
        if (!swapchain) return false;

        dkMemBlockMakerDefaults(
            &memoryMaker, device, kCommandMemorySize);
        memoryMaker.flags =
            DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
        commandMemory = dkMemBlockCreate(&memoryMaker);
        DkCmdBufMaker commandMaker;
        dkCmdBufMakerDefaults(&commandMaker, device);
        commandBuffer = dkCmdBufCreate(&commandMaker);
        if (!commandMemory || !commandBuffer) return false;
        dkCmdBufAddMemory(
            commandBuffer, commandMemory, 0, kCommandMemorySize);

        for (unsigned index = 0; index < kFramebufferCount; ++index) {
            DkImageView view;
            dkImageViewDefaults(&view, &framebuffers[index]);
            dkCmdBufBindRenderTarget(commandBuffer, &view, nullptr);
            bindFramebuffer[index] = dkCmdBufFinishList(commandBuffer);
        }
        dkCmdBufClearColorFloat(commandBuffer, 0, DkColorMask_RGBA,
            0.035f, 0.055f, 0.09f, 1.0f);
        clearScreen = dkCmdBufFinishList(commandBuffer);

        DkQueueMaker queueMaker;
        dkQueueMakerDefaults(&queueMaker, device);
        queueMaker.flags = DkQueueFlags_Graphics;
        queue = dkQueueCreate(&queueMaker);
        return queue != nullptr;
    }

    void render() {
        const int slot = dkQueueAcquireImage(queue, swapchain);
        dkQueueSubmitCommands(queue, bindFramebuffer[slot]);
        dkQueueSubmitCommands(queue, clearScreen);
        dkQueuePresentImage(queue, swapchain, slot);
    }

    void shutdown() {
        if (queue) dkQueueWaitIdle(queue);
        if (queue) dkQueueDestroy(queue);
        if (commandBuffer) dkCmdBufDestroy(commandBuffer);
        if (commandMemory) dkMemBlockDestroy(commandMemory);
        if (swapchain) dkSwapchainDestroy(swapchain);
        if (framebufferMemory) dkMemBlockDestroy(framebufferMemory);
        if (device) dkDeviceDestroy(device);
    }
};

void logStage(std::FILE* log, const char* stage) {
    if (!log) return;
    std::fprintf(log, "%s\n", stage);
    std::fflush(log);
}

}  // namespace

int main() {
    std::FILE* log = std::fopen(
        "sdmc:/switch/switch-auto-v2/v2.log", "w");
    logStage(log, "startup: entered Deko3D probe");

    const auto nvtegra = switch_auto::v2::platform::probeNvTegra();
    logStage(log, nvtegra.available
        ? "startup: NVTEGRA ready"
        : "startup: NVTEGRA unavailable");

    Renderer renderer;
    if (!renderer.initialize()) {
        logStage(log, "startup: Deko3D initialization failed");
        if (log) std::fclose(log);
        return 1;
    }
    logStage(log, "startup: Deko3D ready");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        renderer.render();
    }

    logStage(log, "shutdown: requested");
    renderer.shutdown();
    logStage(log, "shutdown: Deko3D released");
    switch_auto::v2::platform::shutdownNvTegra();
    logStage(log, "shutdown: NVTEGRA released");
    if (log) std::fclose(log);
    return 0;
}
