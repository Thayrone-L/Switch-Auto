#include "switch_auto_v2/platform/nvtegra_video_renderer.hpp"

#include <deko3d.h>
#include <switch.h>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_nvtegra.h>
}

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "switch_auto_v2/platform/nvtegra_h264_decoder.hpp"

namespace switch_auto::v2::platform {
namespace {

// A two-image swapchain frequently blocks acquisition for a complete 16.67 ms
// refresh interval when a decoded frame arrives just after vblank. The third
// image lets decode/presentation overlap without adding a CPU copy.
constexpr unsigned kFramebufferCount = 3;
constexpr unsigned kCodeMemorySize = 128 * 1024;
constexpr unsigned kCommandMemorySize = 32 * 1024;

unsigned alignUp(unsigned value, unsigned alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

struct NvTegraVideoRenderer::Impl {
    struct Mapping {
        void* address = nullptr;
        std::uint32_t size = 0;
        std::uint32_t chromaOffset = 0;
        DkMemBlock memory{};
        DkImage luma{};
        DkImage chroma{};
        DkImageDescriptor lumaDescriptor{};
        DkImageDescriptor chromaDescriptor{};

        ~Mapping() {
            if (memory) dkMemBlockDestroy(memory);
        }
    };

    DkDevice device{};
    DkMemBlock framebufferMemory{};
    DkImage framebuffers[kFramebufferCount]{};
    DkSwapchain swapchain{};
    DkMemBlock codeMemory{};
    unsigned codeOffset = 0;
    DkShader vertexShader{};
    DkShader fragmentShader{};
    DkMemBlock commandMemory[kFramebufferCount]{};
    DkCmdBuf commandBuffer[kFramebufferCount]{};
    DkMemBlock descriptorMemory{};
    DkQueue queue{};
    DkImageLayout lumaLayout{};
    DkImageLayout chromaLayout{};
    std::vector<std::unique_ptr<Mapping>> mappings;
    std::string error;
    int width = 0;
    int height = 0;
    VideoRendererMetrics metrics;

    bool loadShader(DkShader& shader, const char* path) {
        std::FILE* file = std::fopen(path, "rb");
        if (!file) {
            error = std::string("shader missing: ") + path;
            return false;
        }
        std::fseek(file, 0, SEEK_END);
        const long fileSize = std::ftell(file);
        std::rewind(file);
        if (fileSize <= 0 || codeOffset + fileSize > kCodeMemorySize) {
            std::fclose(file);
            error = "invalid shader size";
            return false;
        }
        const auto offset = codeOffset;
        codeOffset += alignUp(static_cast<unsigned>(fileSize),
            DK_SHADER_CODE_ALIGNMENT);
        const auto read = std::fread(
            static_cast<std::uint8_t*>(dkMemBlockGetCpuAddr(codeMemory)) + offset,
            1, static_cast<std::size_t>(fileSize), file);
        std::fclose(file);
        if (read != static_cast<std::size_t>(fileSize)) {
            error = "shader read failed";
            return false;
        }
        DkShaderMaker maker;
        dkShaderMakerDefaults(&maker, codeMemory, offset);
        dkShaderInitialize(&shader, &maker);
        return dkShaderIsValid(&shader);
    }

    Mapping* mappingFor(AVFrame* frame) {
        auto* map = av_nvtegra_frame_get_fbuf_map(frame);
        if (!map || !frame->buf[0] || !frame->data[0] || !frame->data[1]) {
            error = "invalid NVTEGRA surface";
            return nullptr;
        }
        void* address = av_nvtegra_map_get_addr(map);
        const auto size = av_nvtegra_map_get_size(map);
        const auto chromaOffset = static_cast<std::uint32_t>(
            frame->data[1] - frame->data[0]);
        for (const auto& candidate : mappings) {
            if (candidate->address == address && candidate->size == size &&
                candidate->chromaOffset == chromaOffset) {
                return candidate.get();
            }
        }
        auto result = std::make_unique<Mapping>();
        result->address = address;
        result->size = size;
        result->chromaOffset = chromaOffset;
        DkMemBlockMaker memoryMaker;
        dkMemBlockMakerDefaults(&memoryMaker, device, size);
        memoryMaker.flags = DkMemBlockFlags_CpuUncached |
            DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
        memoryMaker.storage = address;
        result->memory = dkMemBlockCreate(&memoryMaker);
        if (!result->memory) {
            error = "NVTEGRA memory import failed";
            return nullptr;
        }
        dkImageInitialize(&result->luma, &lumaLayout, result->memory, 0);
        dkImageInitialize(&result->chroma, &chromaLayout, result->memory,
            chromaOffset);
        DkImageView lumaView;
        DkImageView chromaView;
        dkImageViewDefaults(&lumaView, &result->luma);
        dkImageViewDefaults(&chromaView, &result->chroma);
        dkImageDescriptorInitialize(
            &result->lumaDescriptor, &lumaView, false, false);
        dkImageDescriptorInitialize(
            &result->chromaDescriptor, &chromaView, false, false);
        mappings.push_back(std::move(result));
        return mappings.back().get();
    }
};

NvTegraVideoRenderer::NvTegraVideoRenderer()
    : impl_(std::make_unique<Impl>()) {}

NvTegraVideoRenderer::~NvTegraVideoRenderer() {
    shutdown();
}

bool NvTegraVideoRenderer::initialize(int width, int height) {
    auto& state = *impl_;
    state.width = width;
    state.height = height;
    DkDeviceMaker deviceMaker;
    dkDeviceMakerDefaults(&deviceMaker);
    state.device = dkDeviceCreate(&deviceMaker);
    if (!state.device) return false;

    DkImageLayoutMaker layoutMaker;
    dkImageLayoutMakerDefaults(&layoutMaker, state.device);
    layoutMaker.flags = DkImageFlags_UsageRender |
        DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
    layoutMaker.format = DkImageFormat_RGBA8_Unorm;
    layoutMaker.dimensions[0] = width;
    layoutMaker.dimensions[1] = height;
    DkImageLayout framebufferLayout;
    dkImageLayoutInitialize(&framebufferLayout, &layoutMaker);
    const unsigned imageSize = alignUp(
        static_cast<unsigned>(dkImageLayoutGetSize(&framebufferLayout)),
        dkImageLayoutGetAlignment(&framebufferLayout));
    DkMemBlockMaker memoryMaker;
    dkMemBlockMakerDefaults(
        &memoryMaker, state.device, kFramebufferCount * imageSize);
    memoryMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    state.framebufferMemory = dkMemBlockCreate(&memoryMaker);
    if (!state.framebufferMemory) return false;
    const DkImage* swapchainImages[kFramebufferCount]{};
    for (unsigned index = 0; index < kFramebufferCount; ++index) {
        dkImageInitialize(&state.framebuffers[index], &framebufferLayout,
            state.framebufferMemory, index * imageSize);
        swapchainImages[index] = &state.framebuffers[index];
    }
    DkSwapchainMaker swapchainMaker;
    dkSwapchainMakerDefaults(&swapchainMaker, state.device,
        nwindowGetDefault(), swapchainImages, kFramebufferCount);
    state.swapchain = dkSwapchainCreate(&swapchainMaker);

    dkMemBlockMakerDefaults(&memoryMaker, state.device, kCodeMemorySize);
    memoryMaker.flags = DkMemBlockFlags_CpuUncached |
        DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
    state.codeMemory = dkMemBlockCreate(&memoryMaker);
    if (!state.swapchain || !state.codeMemory ||
        !state.loadShader(state.vertexShader,
            "romfs:/shaders/fullscreen_vsh.dksh") ||
        !state.loadShader(state.fragmentShader,
            "romfs:/shaders/nv12_fsh.dksh")) return false;

    DkCmdBufMaker commandMaker;
    dkCmdBufMakerDefaults(&commandMaker, state.device);
    for (unsigned slot = 0; slot < kFramebufferCount; ++slot) {
        dkMemBlockMakerDefaults(
            &memoryMaker, state.device, kCommandMemorySize);
        state.commandMemory[slot] = dkMemBlockCreate(&memoryMaker);
        state.commandBuffer[slot] = dkCmdBufCreate(&commandMaker);
        if (!state.commandMemory[slot] || !state.commandBuffer[slot]) {
            return false;
        }
    }
    dkMemBlockMakerDefaults(&memoryMaker, state.device,
        kFramebufferCount * DK_MEMBLOCK_ALIGNMENT);
    state.descriptorMemory = dkMemBlockCreate(&memoryMaker);
    DkQueueMaker queueMaker;
    dkQueueMakerDefaults(&queueMaker, state.device);
    queueMaker.flags = DkQueueFlags_Graphics;
    state.queue = dkQueueCreate(&queueMaker);
    if (!state.descriptorMemory || !state.queue) return false;

    dkImageLayoutMakerDefaults(&layoutMaker, state.device);
    layoutMaker.type = DkImageType_2D;
    layoutMaker.format = DkImageFormat_R8_Unorm;
    layoutMaker.dimensions[0] = width;
    layoutMaker.dimensions[1] = height;
    layoutMaker.flags = DkImageFlags_UsageLoadStore |
        DkImageFlags_Usage2DEngine | DkImageFlags_UsageVideo;
    dkImageLayoutInitialize(&state.lumaLayout, &layoutMaker);
    layoutMaker.format = DkImageFormat_RG8_Unorm;
    layoutMaker.dimensions[0] = width / 2;
    layoutMaker.dimensions[1] = height / 2;
    dkImageLayoutInitialize(&state.chromaLayout, &layoutMaker);
    return true;
}

bool NvTegraVideoRenderer::present(
    std::unique_ptr<video::DecodedSurface> surface) {
    auto& state = *impl_;
    auto* hardwareSurface = dynamic_cast<NvTegraDecodedSurface*>(surface.get());
    if (!hardwareSurface || !hardwareSurface->frame()) return false;
    const auto acquireStarted = std::chrono::steady_clock::now();
    const int slot = dkQueueAcquireImage(state.queue, state.swapchain);
    const auto acquireMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - acquireStarted).count());
    state.metrics.totalAcquireMicros += acquireMicros;
    state.metrics.maximumAcquireMicros = std::max(
        state.metrics.maximumAcquireMicros, acquireMicros);
    auto* mapping = state.mappingFor(hardwareSurface->frame());
    if (!mapping) return false;

    auto* descriptorBase = static_cast<std::uint8_t*>(
        dkMemBlockGetCpuAddr(state.descriptorMemory)) +
        slot * DK_MEMBLOCK_ALIGNMENT;
    auto* descriptors = reinterpret_cast<DkImageDescriptor*>(descriptorBase);
    descriptors[0] = mapping->lumaDescriptor;
    descriptors[1] = mapping->chromaDescriptor;
    auto* samplerDescriptor = reinterpret_cast<DkSamplerDescriptor*>(
        descriptors + 2);
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
    sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
    sampler.minFilter = DkFilter_Linear;
    sampler.magFilter = DkFilter_Linear;
    dkSamplerDescriptorInitialize(samplerDescriptor, &sampler);

    const auto descriptorAddress = dkMemBlockGetGpuAddr(
        state.descriptorMemory) + slot * DK_MEMBLOCK_ALIGNMENT;
    auto commandBuffer = state.commandBuffer[slot];
    dkCmdBufClear(commandBuffer);
    dkCmdBufAddMemory(commandBuffer, state.commandMemory[slot], 0,
        kCommandMemorySize);
    DkImageView target;
    dkImageViewDefaults(&target, &state.framebuffers[slot]);
    dkCmdBufBindRenderTarget(commandBuffer, &target, nullptr);
    DkViewport viewport{0.0f, 0.0f, static_cast<float>(state.width),
        static_cast<float>(state.height), 0.0f, 1.0f};
    DkScissor scissor{0, 0, static_cast<unsigned>(state.width),
        static_cast<unsigned>(state.height)};
    dkCmdBufSetViewports(commandBuffer, 0, &viewport, 1);
    dkCmdBufSetScissors(commandBuffer, 0, &scissor, 1);
    dkCmdBufBarrier(commandBuffer, DkBarrier_None,
        DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
    dkCmdBufBindImageDescriptorSet(
        commandBuffer, descriptorAddress, 2);
    dkCmdBufBindSamplerDescriptorSet(commandBuffer,
        descriptorAddress + 2 * sizeof(DkImageDescriptor), 1);
    const DkShader* shaders[]{&state.vertexShader, &state.fragmentShader};
    dkCmdBufBindShaders(commandBuffer,
        DkStageFlag_GraphicsMask, shaders, 2);
    DkRasterizerState rasterizer;
    DkColorState color;
    DkColorWriteState colorWrite;
    dkRasterizerStateDefaults(&rasterizer);
    dkColorStateDefaults(&color);
    dkColorWriteStateDefaults(&colorWrite);
    dkCmdBufBindRasterizerState(commandBuffer, &rasterizer);
    dkCmdBufBindColorState(commandBuffer, &color);
    dkCmdBufBindColorWriteState(commandBuffer, &colorWrite);
    const DkResHandle textures[]{
        dkMakeTextureHandle(0, 0), dkMakeTextureHandle(1, 0)};
    dkCmdBufBindTextures(commandBuffer, DkStage_Fragment, 0, textures, 2);
    dkCmdBufDraw(commandBuffer,
        DkPrimitive_Triangles, 3, 1, 0, 0);
    dkQueueSubmitCommands(state.queue,
        dkCmdBufFinishList(commandBuffer));
    dkQueuePresentImage(state.queue, state.swapchain, slot);
    ++state.metrics.presented;
    return true;
}

void NvTegraVideoRenderer::renderIdle() {
    // Keep the last swapchain image visible until the decoder publishes a
    // newer surface. No redraw is necessary.
}

VideoRendererMetrics NvTegraVideoRenderer::metrics() const {
    return impl_ ? impl_->metrics : VideoRendererMetrics{};
}

void NvTegraVideoRenderer::resetDecoderSurfaces() {
    if (!impl_ || !impl_->device) return;
    if (impl_->queue) dkQueueWaitIdle(impl_->queue);
    impl_->mappings.clear();
}

void NvTegraVideoRenderer::shutdown() {
    if (!impl_ || !impl_->device) return;
    auto& state = *impl_;
    if (state.queue) dkQueueWaitIdle(state.queue);
    state.mappings.clear();
    if (state.queue) dkQueueDestroy(state.queue);
    for (unsigned slot = 0; slot < kFramebufferCount; ++slot) {
        if (state.commandBuffer[slot]) {
            dkCmdBufDestroy(state.commandBuffer[slot]);
        }
    }
    if (state.descriptorMemory) dkMemBlockDestroy(state.descriptorMemory);
    for (auto memory : state.commandMemory) {
        if (memory) dkMemBlockDestroy(memory);
    }
    if (state.codeMemory) dkMemBlockDestroy(state.codeMemory);
    if (state.swapchain) dkSwapchainDestroy(state.swapchain);
    if (state.framebufferMemory) dkMemBlockDestroy(state.framebufferMemory);
    dkDeviceDestroy(state.device);
    state.device = nullptr;
}

const char* NvTegraVideoRenderer::lastError() const {
    return impl_ ? impl_->error.c_str() : "renderer unavailable";
}

}  // namespace switch_auto::v2::platform
