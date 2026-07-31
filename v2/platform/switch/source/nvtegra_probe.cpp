#include "switch_auto_v2/platform/nvtegra_probe.hpp"

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_nvtegra.h>
}

#include <array>

namespace switch_auto::v2::platform {

namespace {

// NVTEGRA and the Borealis OpenGL backend share NVIDIA services. Keep the
// hardware device alive for the process lifetime; releasing it while the UI is
// active tears down resources still required by the graphics context. The V2
// decoder will take ownership of this retained device in the next milestone.
AVBufferRef* retainedDevice = nullptr;

}  // namespace

NvTegraProbeResult probeNvTegra() {
    NvTegraProbeResult result;
    if (retainedDevice) {
        result.available = true;
        result.detail = "NVTEGRA hardware device already initialized";
        return result;
    }

    AVBufferRef* device = nullptr;
    const int createResult = av_hwdevice_ctx_create(
        &device, AV_HWDEVICE_TYPE_NVTEGRA, nullptr, nullptr, 0);
    if (createResult < 0 || !device) {
        std::array<char, AV_ERROR_MAX_STRING_SIZE> error{};
        av_strerror(createResult, error.data(), error.size());
        result.detail = "NVTEGRA unavailable: " + std::string(error.data());
        av_buffer_unref(&device);
        return result;
    }

    auto* deviceContext = reinterpret_cast<AVHWDeviceContext*>(device->data);
    auto* nvtegra = static_cast<AVNVTegraDeviceContext*>(deviceContext->hwctx);
    if (!nvtegra || nvtegra->nvdec_version == 0) {
        result.detail = "NVTEGRA opened without an NVDEC engine";
        av_buffer_unref(&device);
        return result;
    }

    result.available = true;
    av_nvtegra_decode_rev(
        nvtegra->nvdec_version, &result.nvdecMajor, &result.nvdecMinor);
    if (nvtegra->vic_version != 0) {
        av_nvtegra_decode_rev(
            nvtegra->vic_version, &result.vicMajor, &result.vicMinor);
    }
    result.detail = "NVTEGRA hardware device initialized";
    retainedDevice = device;
    return result;
}

void shutdownNvTegra() {
    av_buffer_unref(&retainedDevice);
}

}  // namespace switch_auto::v2::platform
