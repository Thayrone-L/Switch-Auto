#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace switch_auto::platform::switch_platform {

class SwitchH264Decoder {
public:
    SwitchH264Decoder();
    ~SwitchH264Decoder();

    SwitchH264Decoder(const SwitchH264Decoder&) = delete;
    SwitchH264Decoder& operator=(const SwitchH264Decoder&) = delete;

    bool available() const;
    bool decode(const std::uint8_t* data, std::size_t size);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace switch_auto::platform::switch_platform
