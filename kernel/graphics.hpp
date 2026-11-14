#pragma once
#include "hardware.hpp"

namespace graphics {
constexpr int width = 800;
constexpr int height = 600;
struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};
void initialize(const BootInfo& info);
void pixel(int x, int y, u32 color);
void fill(Rect area, u32 color);
void outline(Rect area, u32 color);
void gradient(Rect area, u32 top, u32 bottom);
void shadow(Rect area);
void text(int x, int y, const char* value, u32 color, int scale = 1);
void text_n(int x, int y, const char* value, int count, u32 color, int scale = 1);
void circle(int cx, int cy, int radius, u32 color);
void wallpaper();
void present();
void pointer(int x, int y);
}
