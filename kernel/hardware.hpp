#pragma once
#include "types.hpp"

struct BootInfo {
    u32 magic;
    u32 framebuffer;
    u32 pitch;
    u32 width;
    u32 height;
    u32 bits_per_pixel;
    u32 red_shift;
    u32 green_shift;
    u32 blue_shift;
};
static_assert(sizeof(BootInfo) == 36, "Keep boot/loader.S in sync");

inline void out8(u16 port, u8 value) { asm volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
inline u8 in8(u16 port) { u8 value; asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
inline void relax_cpu() { asm volatile("pause"); }

namespace hardware {
void initialize();
void serial(const char* text);
void serial_number(u32 value);
u32 seconds();
void poll();
bool mouse_available();
[[noreturn]] void reboot();
}

namespace desktop {
void key(u8 scan_code);
void mouse(int dx, int dy, bool left);
}
