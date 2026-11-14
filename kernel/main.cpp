#include "hardware.hpp"
#include "graphics.hpp"
#include "desktop.hpp"

namespace { bool screen_ready; }

extern "C" [[noreturn]] void kernel_fault() {
    hardware::serial("XP96:PANIC CPU exception\n");
    if (screen_ready) {
        graphics::fill({140, 215, 520, 140}, 0x16294c);
        graphics::outline({140, 215, 520, 140}, 0x8db7e8);
        graphics::text(164, 240, "The kernel has stopped.", 0xffffff, 2);
        graphics::text(164, 282, "CPU exception. Please restart the emulator.", 0xcbdcf2);
        graphics::text(164, 312, "Your host computer has not been changed.", 0xcbdcf2);
        graphics::present();
    }
    for (;;) asm volatile("cli; hlt");
}

extern "C" void kernel_main() {
    hardware::initialize();
    const auto& boot = *reinterpret_cast<const BootInfo*>(0x5000);
    if (boot.magic != 0x58503936 || boot.width != 800 || boot.height != 600 ||
        (boot.bits_per_pixel != 24 && boot.bits_per_pixel != 32) ||
        !boot.framebuffer || boot.pitch < boot.width * (boot.bits_per_pixel / 8) ||
        boot.red_shift > 24 || boot.green_shift > 24 || boot.blue_shift > 24) {
        hardware::serial("XP96:PANIC invalid boot information\n");
        for (;;) asm volatile("cli; hlt");
    }
    hardware::serial("XP96:FRAMEBUFFER 800x600x");
    hardware::serial_number(boot.bits_per_pixel);
    hardware::serial("\n");
    graphics::initialize(boot);
    screen_ready = true;
    desktop::initialize();
    desktop::render_if_needed();
    hardware::serial("XP96:DESKTOP_READY\n");
    for (;;) {
        hardware::poll();
        desktop::render_if_needed();
        asm volatile("hlt");
    }
}
