#pragma once

#include <cstdint>
#include <string_view>

namespace switch_auto {

constexpr int kLogicalWidth = 1280;
constexpr int kLogicalHeight = 720;

struct Color {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a = 255;
};

class Canvas {
public:
    Canvas(std::uint32_t* pixels, int width, int height, int stridePixels);

    void clear(Color color);
    void fillRect(int x, int y, int width, int height, Color color);
    void strokeRect(int x, int y, int width, int height, int thickness, Color color);
    void text(int x, int y, std::string_view value, int scale, Color color);
    int textWidth(std::string_view value, int scale) const;

private:
    void glyph(int x, int y, char value, int scale, Color color);

    std::uint32_t* pixels_;
    int width_;
    int height_;
    int stridePixels_;
};

}  // namespace switch_auto

