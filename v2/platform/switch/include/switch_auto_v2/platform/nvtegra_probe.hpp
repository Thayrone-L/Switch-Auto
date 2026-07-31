#pragma once

#include <string>

extern "C" {
struct AVBufferRef;
}

namespace switch_auto::v2::platform {

struct NvTegraProbeResult {
    bool available = false;
    int nvdecMajor = 0;
    int nvdecMinor = 0;
    int vicMajor = 0;
    int vicMinor = 0;
    std::string detail;
};

NvTegraProbeResult probeNvTegra();
AVBufferRef* acquireNvTegraDevice();
void shutdownNvTegra();

}  // namespace switch_auto::v2::platform
