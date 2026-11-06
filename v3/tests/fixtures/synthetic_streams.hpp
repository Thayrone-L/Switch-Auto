#pragma once

#include <array>
#include <cstdint>

namespace switch_auto::test::fixtures {

// Synthetic bytes only: these are not captures and do not model Android Auto.
// They exist to exercise arbitrary fragmentation without private/user data.
inline constexpr std::array<std::uint8_t, 9> ascending_bytes{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

inline constexpr std::array<std::uint8_t, 7> alternating_bytes{
    0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA};

}  // namespace switch_auto::test::fixtures
