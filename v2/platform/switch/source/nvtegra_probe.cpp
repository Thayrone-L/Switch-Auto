#include "switch_auto_v2/platform/nvtegra_probe.hpp"

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_nvtegra.h>
}

#include <array>

namespace switch_auto::v2::platform {

NvTegraProbeResult probeNvTegra() {
    NvTegraProbeResult result;
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
    av_buffer_unref(&device);
    return result;
}

}  // namespace switch_auto::v2::platform

