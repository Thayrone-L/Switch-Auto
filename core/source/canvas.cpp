#include "switch_auto/canvas.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace switch_auto {
namespace {

std::uint32_t pack(Color color) {
    return static_cast<std::uint32_t>(color.r) |
           (static_cast<std::uint32_t>(color.g) << 8U) |
           (static_cast<std::uint32_t>(color.b) << 16U) |
           (static_cast<std::uint32_t>(color.a) << 24U);
}

using Glyph = std::array<std::uint8_t, 7>;

Glyph glyphFor(char raw) {
    const char value = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
    switch (value) {
        case 'A': return {14,17,17,31,17,17,17};
        case 'B': return {30,17,17,30,17,17,30};
        case 'C': return {14,17,16,16,16,17,14};
        case 'D': return {30,17,17,17,17,17,30};
        case 'E': return {31,16,16,30,16,16,31};
        case 'F': return {31,16,16,30,16,16,16};
        case 'G': return {14,17,16,23,17,17,15};
        case 'H': return {17,17,17,31,17,17,17};
        case 'I': return {31,4,4,4,4,4,31};
        case 'J': return {7,2,2,2,18,18,12};
        case 'K': return {17,18,20,24,20,18,17};
        case 'L': return {16,16,16,16,16,16,31};
        case 'M': return {17,27,21,21,17,17,17};
        case 'N': return {17,25,21,19,17,17,17};
        case 'O': return {14,17,17,17,17,17,14};
        case 'P': return {30,17,17,30,16,16,16};
        case 'Q': return {14,17,17,17,21,18,13};
        case 'R': return {30,17,17,30,20,18,17};
        case 'S': return {15,16,16,14,1,1,30};
        case 'T': return {31,4,4,4,4,4,4};
        case 'U': return {17,17,17,17,17,17,14};
        case 'V': return {17,17,17,17,17,10,4};
        case 'W': return {17,17,17,21,21,21,10};
        case 'X': return {17,17,10,4,10,17,17};
        case 'Y': return {17,17,10,4,4,4,4};
        case 'Z': return {31,1,2,4,8,16,31};
        case '0': return {14,17,19,21,25,17,14};
        case '1': return {4,12,4,4,4,4,14};
        case '2': return {14,17,1,2,4,8,31};
        case '3': return {30,1,1,14,1,1,30};
        case '4': return {2,6,10,18,31,2,2};
        case '5': return {31,16,16,30,1,1,30};
        case '6': return {14,16,16,30,17,17,14};
        case '7': return {31,1,2,4,8,8,8};
        case '8': return {14,17,17,14,17,17,14};
        case '9': return {14,17,17,15,1,1,14};
        case ':': return {0,4,4,0,4,4,0};
        case '-': return {0,0,0,31,0,0,0};
        case '.': return {0,0,0,0,0,4,4};
        case '/': return {1,2,2,4,8,8,16};
        case '+': return {0,4,4,31,4,4,0};
        case '%': return {17,2,4,4,8,16,17};
        default: return {0,0,0,0,0,0,0};
    }
}

}  // namespace

Canvas::Canvas(std::uint32_t* pixels, int width, int height, int stridePixels)
    : pixels_(pixels), width_(width), height_(height), stridePixels_(stridePixels) {}

void Canvas::clear(Color color) {
    fillRect(0, 0, width_, height_, color);
}

void Canvas::fillRect(int x, int y, int width, int height, Color color) {
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(width_, x + width);
    const int bottom = std::min(height_, y + height);
    if (right <= left || bottom <= top) return;
    const std::uint32_t packed = pack(color);
    for (int row = top; row < bottom; ++row) {
        std::fill(pixels_ + row * stridePixels_ + left,
                  pixels_ + row * stridePixels_ + right, packed);
    }
}

void Canvas::strokeRect(int x, int y, int width, int height, int thickness, Color color) {
    fillRect(x, y, width, thickness, color);
    fillRect(x, y + height - thickness, width, thickness, color);
    fillRect(x, y, thickness, height, color);
    fillRect(x + width - thickness, y, thickness, height, color);
}

void Canvas::glyph(int x, int y, char value, int scale, Color color) {
    const Glyph rows = glyphFor(value);
    for (int row = 0; row < 7; ++row) {
        for (int column = 0; column < 5; ++column) {
            if ((rows[row] & (1U << (4 - column))) != 0) {
                fillRect(x + column * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void Canvas::text(int x, int y, std::string_view value, int scale, Color color) {
    int cursor = x;
    for (const char character : value) {
        glyph(cursor, y, character, scale, color);
        cursor += 6 * scale;
    }
}

int Canvas::textWidth(std::string_view value, int scale) const {
    return value.empty() ? 0 : static_cast<int>(value.size()) * 6 * scale - scale;
}

}  // namespace switch_auto
