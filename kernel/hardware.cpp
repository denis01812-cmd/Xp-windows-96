#include "hardware.hpp"

namespace {
struct [[gnu::packed]] Gate { u16 low, selector; u8 reserved, flags; u16 high; };
struct [[gnu::packed]] IdtPointer { u16 limit; u32 base; };
Gate idt[256];
extern "C" void fault_stub();
extern "C" void timer_stub();
volatile u32 timer_ticks;
bool has_mouse;
u8 mouse_packet[3];
int mouse_index;

bool wait_write() {
    for (int i = 0; i < 100000; ++i) {
        if (!(in8(0x64) & 2)) return true;
        relax_cpu();
    }
    return false;
}
bool controller(u8 command) {
    if (!wait_write()) return false;
    out8(0x64, command);
    return true;
}
bool data(u8 value) {
    if (!wait_write()) return false;
    out8(0x60, value);
    return true;
}
bool read_response(u8& value) {
    for (int i = 0; i < 100000; ++i) {
        if (in8(0x64) & 1) { value = in8(0x60); return true; }
        relax_cpu();
    }
    return false;
}
bool device_command(u8 command, bool mouse = false) {
    u8 response = 0;
    return (!mouse || controller(0xd4)) && data(command) && read_response(response) && response == 0xfa;
}

}

namespace hardware {
void serial(const char* text) {
    for (; *text; ++text) {
        // A missing UART must never prevent booting.
        for (int wait = 0; wait < 10000 && !(in8(0x3fd) & 0x20); ++wait) relax_cpu();
        out8(0x3f8, static_cast<u8>(*text));
    }
}
void serial_number(u32 value) { char text[11]; decimal(value, text); serial(text); }

void initialize() {
    out8(0x3f9, 0x00);
    out8(0x3fb, 0x80);
    out8(0x3f8, 0x01);
    out8(0x3f9, 0x00);
    out8(0x3fb, 0x03);
    out8(0x3fa, 0xc7);
    out8(0x3fc, 0x0b);
    serial("XP96:BOOT C++ protected-mode kernel\n");

    const u32 handler = reinterpret_cast<u32>(&fault_stub);
    for (auto& gate : idt) gate = {static_cast<u16>(handler), 0x08, 0, 0x8e, static_cast<u16>(handler >> 16)};
    const IdtPointer pointer = {sizeof(idt) - 1, reinterpret_cast<u32>(idt)};
    asm volatile("lidt %0" : : "m"(pointer));
    const u32 timer = reinterpret_cast<u32>(&timer_stub);
    idt[32] = {static_cast<u16>(timer), 0x08, 0, 0x8e, static_cast<u16>(timer >> 16)};
    // Remap both PICs, then enable only IRQ0. PS/2 is polled separately.
    out8(0x20, 0x11); out8(0x80, 0);
    out8(0xa0, 0x11); out8(0x80, 0);
    out8(0x21, 0x20); out8(0x80, 0);
    out8(0xa1, 0x28); out8(0x80, 0);
    out8(0x21, 0x04); out8(0x80, 0);
    out8(0xa1, 0x02); out8(0x80, 0);
    out8(0x21, 0x01); out8(0x80, 0);
    out8(0xa1, 0x01); out8(0x80, 0);
    out8(0x21, 0xff);
    out8(0xa1, 0xff);
    constexpr u16 divisor = 11932; // Approximately 100 Hz.
    out8(0x43, 0x34);
    out8(0x40, divisor & 255);
    out8(0x40, divisor >> 8);

    controller(0xad);
    controller(0xa7);
    for (int i = 0; i < 64 && (in8(0x64) & 1); ++i) in8(0x60);
    u8 configuration = 0x40;
    if (controller(0x20)) read_response(configuration);
    configuration = static_cast<u8>((configuration | 0x40) & ~0x03);
    if (controller(0x60)) data(configuration);
    controller(0xae);
    controller(0xa8);
    device_command(0xf5);
    device_command(0xf0);
    device_command(0x02);
    device_command(0xf4);
    has_mouse = device_command(0xf6, true) && device_command(0xf4, true);
    serial(has_mouse ? "XP96:INPUT_READY keyboard+mouse\n" : "XP96:INPUT_READY keyboard (mouse unavailable)\n");
    out8(0x21, 0xfe);
    asm volatile("sti");
}

u32 seconds() { return timer_ticks / 100; }
bool mouse_available() { return has_mouse; }

void poll() {
    // A budget keeps a busy PS/2 device from starving rendering.
    for (int budget = 0; budget < 64; ++budget) {
        const u8 status = in8(0x64);
        if (!(status & 1)) break;
        const u8 value = in8(0x60);
        if (status & 0xc0) { mouse_index = 0; continue; }
        if (!(status & 0x20)) { desktop::key(value); continue; }
        if (!has_mouse) continue;
        if (mouse_index == 0 && !(value & 0x08)) continue;
        mouse_packet[mouse_index++] = value;
        if (mouse_index != 3) continue;
        mouse_index = 0;
        if (mouse_packet[0] & 0xc0) continue;
        const int dx = mouse_packet[1] - ((mouse_packet[0] & 0x10) ? 256 : 0);
        const int dy = mouse_packet[2] - ((mouse_packet[0] & 0x20) ? 256 : 0);
        desktop::mouse(dx, -dy, mouse_packet[0] & 1);
    }
}

[[noreturn]] void reboot() {
    serial("XP96:REBOOT\n");
    controller(0xfe);
    // If the keyboard-controller reset is unavailable, force a triple fault.
    const IdtPointer empty = {0, 0};
    asm volatile("lidt %0; int $3" : : "m"(empty));
    for (;;) asm volatile("cli; hlt");
}
}

extern "C" void timer_tick() {
    ++timer_ticks;
    out8(0x20, 0x20);
}
