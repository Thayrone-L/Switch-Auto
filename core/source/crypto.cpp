#include "switch_auto/crypto.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace switch_auto::crypto {
namespace {

constexpr std::array<std::uint32_t, 64> kRound{
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

std::uint32_t rotate(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

std::array<std::uint8_t, 32> sha256(const std::vector<std::uint8_t>& input) {
    std::vector<std::uint8_t> data = input;
    const std::uint64_t bitLength = static_cast<std::uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) {
        data.push_back(static_cast<std::uint8_t>(bitLength >> shift));
    }
    std::array<std::uint32_t, 8> hash{
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (std::size_t offset = 0; offset < data.size(); offset += 64) {
        std::array<std::uint32_t, 64> words{};
        for (int index = 0; index < 16; ++index) {
            const std::size_t base = offset + static_cast<std::size_t>(index) * 4;
            words[index] = (static_cast<std::uint32_t>(data[base]) << 24) |
                (static_cast<std::uint32_t>(data[base + 1]) << 16) |
                (static_cast<std::uint32_t>(data[base + 2]) << 8) | data[base + 3];
        }
        for (int index = 16; index < 64; ++index) {
            const std::uint32_t s0 = rotate(words[index - 15], 7) ^
                rotate(words[index - 15], 18) ^ (words[index - 15] >> 3);
            const std::uint32_t s1 = rotate(words[index - 2], 17) ^
                rotate(words[index - 2], 19) ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        auto [a,b,c,d,e,f,g,h] = hash;
        for (int index = 0; index < 64; ++index) {
            const std::uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + s1 + choice + kRound[index] + words[index];
            const std::uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + majority;
            h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        hash[0]+=a; hash[1]+=b; hash[2]+=c; hash[3]+=d;
        hash[4]+=e; hash[5]+=f; hash[6]+=g; hash[7]+=h;
    }
    std::array<std::uint8_t, 32> output{};
    for (std::size_t index = 0; index < hash.size(); ++index) {
        output[index*4] = static_cast<std::uint8_t>(hash[index] >> 24);
        output[index*4+1] = static_cast<std::uint8_t>(hash[index] >> 16);
        output[index*4+2] = static_cast<std::uint8_t>(hash[index] >> 8);
        output[index*4+3] = static_cast<std::uint8_t>(hash[index]);
    }
    return output;
}

std::vector<std::uint8_t> decodeHex(std::string_view value) {
    if (value.size() % 2 != 0) return {};
    auto digit = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> bytes;
    bytes.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        const int high = digit(value[index]);
        const int low = digit(value[index + 1]);
        if (high < 0 || low < 0) return {};
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return bytes;
}

std::string encodeHex(const std::array<std::uint8_t, 32>& bytes) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string output(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        output[index * 2] = alphabet[bytes[index] >> 4];
        output[index * 2 + 1] = alphabet[bytes[index] & 0x0f];
    }
    return output;
}

}  // namespace

std::string hmacSha256Hex(std::string_view hexKey, std::string_view message) {
    std::vector<std::uint8_t> key = decodeHex(hexKey);
    if (key.empty()) return {};
    if (key.size() > 64) {
        const auto digest = sha256(key);
        key.assign(digest.begin(), digest.end());
    }
    key.resize(64, 0);
    std::vector<std::uint8_t> inner(64);
    std::vector<std::uint8_t> outer(64);
    for (std::size_t index = 0; index < 64; ++index) {
        inner[index] = key[index] ^ 0x36;
        outer[index] = key[index] ^ 0x5c;
    }
    inner.insert(inner.end(), message.begin(), message.end());
    const auto innerHash = sha256(inner);
    outer.insert(outer.end(), innerHash.begin(), innerHash.end());
    return encodeHex(sha256(outer));
}

bool constantTimeEquals(std::string_view left, std::string_view right) {
    std::size_t difference = left.size() ^ right.size();
    const std::size_t maximum = left.size() > right.size() ? left.size() : right.size();
    for (std::size_t index = 0; index < maximum; ++index) {
        const unsigned char a = index < left.size() ? left[index] : 0;
        const unsigned char b = index < right.size() ? right[index] : 0;
        difference |= a ^ b;
    }
    return difference == 0;
}

}  // namespace switch_auto::crypto
