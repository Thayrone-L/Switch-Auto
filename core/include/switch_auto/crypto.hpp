#pragma once

#include <string>
#include <string_view>

namespace switch_auto::crypto {

std::string hmacSha256Hex(std::string_view hexKey, std::string_view message);
bool constantTimeEquals(std::string_view left, std::string_view right);

}  // namespace switch_auto::crypto
