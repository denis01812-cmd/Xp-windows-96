#include "graphics.hpp"
#include "font8x8.hpp"

namespace {
u32 canvas[graphics::width * graphics::height];
u32 background[graphics::width * graphics::height];
BootInfo video;

u32 blend(u32 a, u32 b, int amount, int total) {
    const int red = (((a >> 16) & 255) * (total - amount) + ((b >> 16) & 255) * amount) / total;
    const int green = (((a >> 8) & 255) * (total - amount) + ((b >> 8) & 255) * amount) / total;
    const int blue = ((a & 255) * (total - amount) + (b & 255) * amount) / total;
    return static_cast<u32>((red << 16) | (green << 8) | blue);
}
}

namespace graphics {
void pixel(int x, int y, u32 color) {
    if (x >= 0 && y >= 0 && x < width && y < height) canvas[y * width + x] = color;
}
void fill(Rect area, u32 color) {
    const int right = clamp(area.x + area.w, 0, width);
    const int bottom = clamp(area.y + area.h, 0, height);
    for (int y = clamp(area.y, 0, height); y < bottom; ++y)
        for (int x = clamp(area.x, 0, width); x < right; ++x) canvas[y * width + x] = color;
}
void outline(Rect area, u32 color) {
    fill({area.x, area.y, area.w, 1}, color);
    fill({area.x, area.y + area.h - 1, area.w, 1}, color);
    fill({area.x, area.y, 1, area.h}, color);
    fill({area.x + area.w - 1, area.y, 1, area.h}, color);
}
void gradient(Rect area, u32 top, u32 bottom) {
    if (area.h <= 0) return;
    for (int y = 0; y < area.h; ++y) fill({area.x, area.y + y, area.w, 1}, blend(top, bottom, y, area.h));
}
void shadow(Rect area) {
    for (int y = clamp(area.y + 5, 0, height); y < clamp(area.y + area.h + 6, 0, height); ++y)
        for (int x = clamp(area.x + 6, 0, width); x < clamp(area.x + area.w + 7, 0, width); ++x) {
            const u32 color = canvas[y * width + x];
            canvas[y * width + x] = ((color & 0xfefefe) >> 1) + ((color & 0xfcfcfc) >> 2);
        }
}
void text_n(int x, int y, const char* value, int count, u32 color, int scale) {
    for (int n = 0; n < count && value[n]; ++n) {
        const u8 glyph = static_cast<u8>(value[n]);
        if (glyph >= 128) continue;
        for (int row = 0; row < 8; ++row)
            for (int column = 0; column < 8; ++column)
                if (font8x8_basic[glyph][row] & (1 << column))
                    fill({x + n * 8 * scale + column * scale, y + row * scale, scale, scale}, color);
    }
}
void text(int x, int y, const char* value, u32 color, int scale) { text_n(x, y, value, length(value), color, scale); }
void circle(int cx, int cy, int radius, u32 color) {
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius) pixel(cx + x, cy + y, color);
}

void initialize(const BootInfo& info) {
    video = info;
    gradient({0, 0, width, height}, 0x76b9e8, 0xe8f2d5);
    // Original procedural wallpaper: no Windows images or other proprietary assets.
    for (int x = 0; x < width; ++x) {
        const int far_hill = 344 + ((x - 600) * (x - 600)) / 1900;
        const int near_hill = 363 + ((x - 105) * (x - 105)) / 2100;
        for (int y = far_hill; y < height; ++y)
            pixel(x, y, blend(0x81b87b, 0x62975b, y - far_hill, height - far_hill));
        for (int y = near_hill; y < height; ++y)
            pixel(x, y, blend(0x75b84d, 0x286b45, y - near_hill, height - near_hill));
    }
    // Light cloud bands rendered into the background only once.
    for (int x = 340; x < 670; ++x) {
        const int distance = (x - 505) * (x - 505);
        const int thickness = 10 - distance / 2800;
        if (thickness > 0)
            for (int y = 0; y < thickness; ++y) {
                const int index = (104 + y) * width + x;
                canvas[index] = blend(canvas[index], 0xf4fbff, 1, 3);
            }
    }
    text(582, 496, "XP / 96", 0xf0f7e8, 3);
    text(586, 528, "A FRESH START. AGAIN.", 0xd6ebcb);
    fill({654, 20, 124, 22}, 0x5798c6);
    text(668, 27, "PREVIEW 0.1", 0xeaf6ff);
    memcpy(background, canvas, sizeof(canvas));
}

void wallpaper() { memcpy(canvas, background, sizeof(canvas)); }

void present() {
    // VBE uses BGR on the emulators we support; mask positions are still honored.
    const u32 bytes = video.bits_per_pixel / 8;
    auto* target = reinterpret_cast<volatile u8*>(video.framebuffer);
    for (int y = 0; y < height; ++y) {
        volatile u8* row = target + y * video.pitch;
        for (int x = 0; x < width; ++x) {
            const u32 color = canvas[y * width + x];
            const u32 native = (((color >> 16) & 255) << video.red_shift) |
                               (((color >> 8) & 255) << video.green_shift) |
                               ((color & 255) << video.blue_shift);
            for (u32 byte = 0; byte < bytes; ++byte) row[x * bytes + byte] = static_cast<u8>(native >> (byte * 8));
        }
    }
}

void pointer(int x, int y) {
    static constexpr const char* shape[] = {
        "X           ", "XX          ", "XOX         ", "XOOX        ",
        "XOOOX       ", "XOOOOX      ", "XOOOOOX     ", "XOOOOOOX    ",
        "XOOOOOOOX   ", "XOOOOOOOOX  ", "XOOOOOXXXXX ", "XOOXOOX     ",
        "XOX XOOX    ", "XX  XOOX    ", "X    XOOX   ", "     XOOX   ",
        "      XX    "
    };
    for (int row = 0; row < 17; ++row)
        for (int column = 0; column < 12; ++column)
            if (shape[row][column] != ' ') pixel(x + column + 1, y + row + 2, 0x344b5b);
    for (int row = 0; row < 17; ++row)
        for (int column = 0; column < 12; ++column)
            if (shape[row][column] != ' ') pixel(x + column, y + row, shape[row][column] == 'X' ? 0x15273b : 0xffffff);
}
}
