#include "desktop.hpp"
#include "graphics.hpp"
#include "hardware.hpp"

namespace {
using graphics::Rect;
using graphics::fill;
using graphics::outline;
using graphics::gradient;
using graphics::text;
using graphics::text_n;
using graphics::circle;

constexpr u32 ink = 0x22364e;
constexpr u32 muted = 0x68788a;
constexpr u32 blue = 0x225ed1;
constexpr u32 paper = 0xf8f7f2;
constexpr int taskbar_y = 560;
constexpr int start_width = 132;
constexpr int task_left = 142;
constexpr int app_count = 5;
enum App { Welcome, Files, Terminal, Notes, System };
struct Window { Rect bounds; bool open; bool minimized; };
Window windows[app_count] = {
    {{210, 72, 548, 422}, true, false},
    {{194, 116, 528, 354}, false, false},
    {{156, 110, 616, 402}, false, false},
    {{240, 128, 514, 386}, false, false},
    {{252, 152, 478, 328}, false, false}
};
constexpr const char* titles[] = {"Welcome home", "My computer", "Terminal", "Untitled - Notes", "System information"};
constexpr const char* app_names[] = {"Welcome", "My computer", "Terminal", "Notes", "System info"};
constexpr const char* app_ids[] = {"welcome", "files", "terminal", "notes", "system"};
int order[app_count] = {0, 1, 2, 3, 4};
bool dirty = true;
bool start_open;
int mouse_x = 682;
int mouse_y = 450;
bool mouse_left;
int dragging = -1;
int drag_x, drag_y;
u32 last_second;
bool shift_left, shift_right, control, alt, caps_lock, extended;

constexpr int terminal_columns = 70;
constexpr int terminal_capacity = 48;
char terminal_lines[terminal_capacity][terminal_columns + 1];
int terminal_count;
char command[128];
int command_length;
char previous_command[128];
char notes[2048] = "A fresh page. A fresh start.\n\nThis text lives in RAM, inside your own OS.\nTry typing here with the US keyboard layout.\n\nNothing is saved to disk yet.\n";
int notes_length;
bool notes_warning;

int active_app() {
    for (int i = app_count - 1; i >= 0; --i) {
        const int app = order[i];
        if (windows[app].open && !windows[app].minimized) return app;
    }
    return -1;
}
void focus(int app) {
    int position = 0;
    while (position < app_count && order[position] != app) ++position;
    for (int i = position; i < app_count - 1; ++i) order[i] = order[i + 1];
    order[app_count - 1] = app;
    dirty = true;
}
void open_app(int app) {
    windows[app].open = true;
    windows[app].minimized = false;
    start_open = false;
    focus(app);
    hardware::serial("XP96:OPEN "); hardware::serial(app_ids[app]); hardware::serial("\n");
}
void close_app(int app) {
    if (app < 0) return;
    windows[app].open = false;
    dragging = -1;
    dirty = true;
    hardware::serial("XP96:CLOSE "); hardware::serial(app_ids[app]); hardware::serial("\n");
}

void terminal_line(const char* value) {
    hardware::serial("XP96:TERM "); hardware::serial(value); hardware::serial("\n");
    do {
        if (terminal_count == terminal_capacity) {
            memmove(terminal_lines, terminal_lines + 1, sizeof(terminal_lines) - sizeof(terminal_lines[0]));
            --terminal_count;
        }
        int count = 0;
        while (value[count] && value[count] != '\n' && count < terminal_columns) ++count;
        for (int i = 0; i < count; ++i) terminal_lines[terminal_count][i] = value[i];
        terminal_lines[terminal_count++][count] = '\0';
        value += count;
        if (*value == '\n') ++value;
    } while (*value);
    dirty = true;
}
void terminal_number(const char* label, u32 value, const char* suffix) {
    char line[96];
    copy_text(line, label, sizeof(line));
    int position = length(line);
    decimal(value, line + position);
    position = length(line);
    copy_text(line + position, suffix, sizeof(line) - position);
    terminal_line(line);
}
void execute() {
    char prompt[136] = "> ";
    copy_text(prompt + 2, command, sizeof(prompt) - 2);
    terminal_line(prompt);
    hardware::serial("XP96:CMD "); hardware::serial(command); hardware::serial("\n");
    copy_text(previous_command, command, sizeof(previous_command));
    if (equal(command, "help")) {
        terminal_line("help     about    info     uptime    clear");
        terminal_line("ls       cat NAME          echo TEXT");
        terminal_line("notes    reboot");
        terminal_line("F1 Welcome / F2 Files / F3 Terminal / F4 Notes / F5 System");
    } else if (equal(command, "clear")) {
        terminal_count = 0;
    } else if (equal(command, "about")) {
        terminal_line("XP / 96 - independent hobby OS, version 0.1.0");
        terminal_line("C++ kernel. Python build tools. Not Microsoft Windows.");
    } else if (equal(command, "info")) {
        terminal_line("CPU: x86 / 32-bit protected mode / flat memory");
        terminal_line("Display: VBE 800 x 600 / software compositor");
        terminal_line("Timer: PIT 100 Hz / input: PS/2 / log: COM1");
        terminal_line("No processes, disk filesystem, networking or heap yet.");
    } else if (equal(command, "uptime")) {
        terminal_number("Uptime: ", hardware::seconds(), " seconds");
    } else if (equal(command, "ls")) {
        terminal_line("WELCOME.TXT   SYSTEM.TXT   NOTES.TXT");
        terminal_line("These are built-in virtual files, not files on a disk.");
    } else if (equal(command, "cat WELCOME.TXT")) {
        terminal_line("Welcome to XP / 96! Open apps from Start or the desktop.");
        terminal_line("Drag title bars. Minimize and restore via the taskbar.");
        terminal_line("This is a tiny OS, not a Windows-compatible system.");
    } else if (equal(command, "cat SYSTEM.TXT")) {
        terminal_line("XP / 96 v0.1.0 | BIOS + x86 | C++17 freestanding");
        terminal_line("Python runs on the host only. Assembly handles boot/IRQs.");
    } else if (equal(command, "cat NOTES.TXT")) {
        terminal_line(notes);
    } else if (starts_with(command, "cat ")) {
        terminal_line("File not found. Use ls; names are case-sensitive.");
    } else if (starts_with(command, "echo ")) {
        terminal_line(command + 5);
    } else if (equal(command, "notes")) {
        open_app(Notes);
    } else if (equal(command, "reboot")) {
        hardware::reboot();
    } else if (command_length) {
        terminal_line("Unknown command. Type help.");
    }
    command[0] = '\0';
    command_length = 0;
    dirty = true;
}

void flag(int x, int y, int size) {
    fill({x, y, size, size}, 0xf16a40);
    fill({x + size + 2, y - 2, size, size}, 0x86c744);
    fill({x, y + size + 2, size, size}, 0x55b8ee);
    fill({x + size + 2, y + size, size, size}, 0xffd55b);
}
void icon(int app, int x, int y, bool small = false) {
    if (small) {
        const u32 colors[] = {0x9bdb78, 0xffd363, 0x182c4c, 0xfff7cf, 0x89c4f5};
        fill({x, y, 15, 14}, colors[app]);
        outline({x, y, 15, 14}, 0x234879);
        if (app == Terminal) text(x + 3, y + 3, ">", 0xffffff);
        else fill({x + 3, y + 4, 9, 2}, 0xffffff);
        return;
    }
    if (app == Files || app == Welcome) {
        fill({x + 3, y + 3, 20, 9}, 0xd39524);
        fill({x + 1, y + 10, 43, 29}, 0xbb8225);
        gradient({x + 3, y + 9, 40, 27}, 0xffde78, 0xeeb74c);
        fill({x + 5, y + 11, 36, 2}, 0xffefb7);
        fill({x + 6, y + 30, 31, 2}, 0xe7aa3d);
    } else if (app == Terminal) {
        fill({x + 1, y + 2, 43, 35}, 0x14325c);
        outline({x + 1, y + 2, 43, 35}, 0xb5d5ed);
        gradient({x + 3, y + 4, 39, 6}, 0x6a9bdb, 0x2b64b6);
        text(x + 7, y + 17, ">_", 0xd5f8d8, 2);
    } else if (app == Notes) {
        fill({x + 8, y + 1, 31, 39}, 0x4a6981);
        fill({x + 6, y, 31, 38}, 0xfffdf0);
        fill({x + 6, y + 1, 31, 7}, 0xf2ca61);
        for (int row = 0; row < 4; ++row) fill({x + 12, y + 14 + row * 5, 19, 1}, 0x87a4b9);
        fill({x + 10, y + 10, 1, 25}, 0xe8a596);
    } else {
        gradient({x + 3, y + 2, 40, 28}, 0xe0e9f3, 0x91acc3);
        outline({x + 3, y + 2, 40, 28}, 0x3c5e7f);
        gradient({x + 6, y + 5, 34, 21}, 0x74b9e5, 0x2a66b6);
        fill({x + 20, y + 30, 7, 6}, 0x8ca7bc);
        fill({x + 13, y + 36, 21, 3}, 0xd5e1eb);
        flag(x + 16, y + 11, 5);
    }
}
void button(Rect area, const char* label, bool primary = false) {
    const bool hover = area.contains(mouse_x, mouse_y);
    gradient(area, primary ? (hover ? 0x488bea : 0x397cde) : (hover ? 0xffffff : 0xfdfcf8), primary ? 0x225ac1 : 0xe9e8e0);
    outline(area, primary ? 0x1c4b9a : 0xb6bfca);
    fill({area.x + 1, area.y + 1, area.w - 2, 1}, primary ? 0x8bb8ef : 0xffffff);
    text(area.x + (area.w - length(label) * 8) / 2, area.y + (area.h - 8) / 2, label, primary ? 0xffffff : ink);
}

void welcome_content(Rect r) {
    const int x = r.x + 25;
    text(x, r.y + 55, "YOUR DESKTOP, REIMAGINED", muted);
    text(x, r.y + 80, "A familiar start.", ink, 2);
    text(x, r.y + 111, "A little blue. A little green. All yours.", muted);
    const int card_y = r.y + 145;
    fill({x, card_y, 240, 92}, 0xe8f0fb);
    outline({x, card_y, 240, 92}, 0xcbdcf2);
    fill({x + 254, card_y, 244, 92}, 0xeaf1e3);
    outline({x + 254, card_y, 244, 92}, 0xd1dfc5);
    icon(System, x + 14, card_y + 14);
    text(x + 70, card_y + 20, "C++ KERNEL", 0x2759a1);
    text(x + 70, card_y + 42, "32-bit x86", ink);
    text(x + 16, card_y + 72, "BOOTED. NOT SIMULATED UI.", 0x53739b);
    text(x + 270, card_y + 20, "SMALL BY DESIGN", 0x467042);
    text(x + 270, card_y + 42, "Built from source.", ink);
    text(x + 270, card_y + 62, "Made to explore.", 0x5f7c52);
    text(x, r.y + 260, "WHERE WOULD YOU LIKE TO GO?", muted);
    button({x, r.y + 282, 240, 39}, "My computer", true);
    button({x + 254, r.y + 282, 244, 39}, "Open terminal");
    fill({x, r.y + 346, 498, 1}, 0xdcded9);
    circle(x + 4, r.y + 370, 4, 0x68a34a);
    text(x + 16, r.y + 367, "SYSTEM READY", 0x4e7546);
    text(x + 264, r.y + 367, "F1-F5 TO EXPLORE", muted);
    text(x, r.y + 393, "INDEPENDENT HOBBY OS  /  PREVIEW 0.1.0", 0x8b968f);
}
void files_content(Rect r) {
    fill({r.x + 3, r.y + 31, r.w - 6, 43}, 0xe8edf5);
    text(r.x + 20, r.y + 48, "XP96 / BUILT-IN FILES", 0x416180);
    text(r.x + 22, r.y + 93, "NAME", muted);
    text(r.x + 296, r.y + 93, "KIND", muted);
    constexpr const char* filenames[] = {"WELCOME.TXT", "SYSTEM.TXT", "NOTES.TXT"};
    constexpr const char* kinds[] = {"Getting started", "System details", "RAM scratchpad"};
    const int apps[] = {Welcome, System, Notes};
    for (int i = 0; i < 3; ++i) {
        Rect row = {r.x + 16, r.y + 113 + i * 53, r.w - 32, 47};
        fill(row, row.contains(mouse_x, mouse_y) ? 0xe1ecfa : (i % 2 ? 0xf0f2ef : 0xffffff));
        icon(apps[i], row.x + 10, row.y + 16, true);
        text(row.x + 38, row.y + 19, filenames[i], ink);
        text(r.x + 296, row.y + 19, kinds[i], muted);
    }
    text(r.x + 22, r.y + 292, "Click a file to open it.", muted);
    fill({r.x + 3, r.y + r.h - 30, r.w - 6, 27}, 0xe9e8e0);
    text(r.x + 16, r.y + r.h - 20, "3 virtual files / no disk filesystem yet", 0x737d82);
}
void terminal_content(Rect r) {
    fill({r.x + 3, r.y + 31, r.w - 6, r.h - 34}, 0x121e30);
    text(r.x + 17, r.y + 48, "XP / 96 SHELL", 0x8fb7eb);
    text(r.x + 361, r.y + 48, "TYPE help TO BEGIN", 0x607e9f);
    fill({r.x + 16, r.y + 67, r.w - 32, 1}, 0x2a3a51);
    const int visible = (r.h - 119) / 14;
    const int first = terminal_count > visible ? terminal_count - visible : 0;
    for (int i = first; i < terminal_count; ++i)
        text(r.x + 18, r.y + 81 + (i - first) * 14, terminal_lines[i], 0xd1e2ef);
    const int y = r.y + r.h - 27;
    text(r.x + 18, y, ">", 0x9bda9d);
    const int offset = command_length > 67 ? command_length - 67 : 0;
    text_n(r.x + 34, y, command + offset, 67, 0xf0f6ff);
    if (!(hardware::seconds() & 1)) fill({r.x + 34 + (command_length - offset) * 8, y + 9, 7, 2}, 0x9bda9d);
}
void notes_content(Rect r) {
    fill({r.x + 3, r.y + 31, r.w - 6, 36}, 0xf1ecd9);
    text(r.x + 19, r.y + 46, "SCRATCHPAD", 0x7e724f);
    text(r.x + 299, r.y + 46, "RAM ONLY", 0xaa7534);
    fill({r.x + 4, r.y + 69, r.w - 8, r.h - 101}, 0xfffdf5);
    constexpr int columns = 58;
    const int visible = (r.h - 112) / 14;
    int line_count = 1;
    int column = 0;
    for (int i = 0; i < notes_length; ++i) {
        if (notes[i] == '\n') { ++line_count; column = 0; }
        else if (++column == columns) { ++line_count; column = 0; }
    }
    const int first = line_count > visible ? line_count - visible : 0;
    int row = 0;
    column = 0;
    for (int i = 0; i < notes_length; ++i) {
        if (notes[i] == '\n') { ++row; column = 0; continue; }
        if (row >= first) text_n(r.x + 20 + column * 8, r.y + 84 + (row - first) * 14, notes + i, 1, ink);
        if (++column == columns) { ++row; column = 0; }
    }
    if (active_app() == Notes && !(hardware::seconds() & 1))
        fill({r.x + 20 + column * 8, r.y + 83 + (row - first) * 14, 1, 12}, 0x766344);
    fill({r.x + 3, r.y + r.h - 30, r.w - 6, 27}, 0xf1ecd9);
    text(r.x + 16, r.y + r.h - 20, notes_warning ? "Not saved: disk writes are not implemented." : "US keyboard / changes are lost on reboot", 0x887648);
}
void system_content(Rect r) {
    icon(System, r.x + 25, r.y + 56);
    text(r.x + 88, r.y + 58, "XP / 96", ink, 2);
    text(r.x + 89, r.y + 87, "INDEPENDENT HOBBY OS", muted);
    fill({r.x + 24, r.y + 119, r.w - 48, 1}, 0xd7dfdf);
    constexpr const char* labels[] = {"Version", "Architecture", "Graphics", "Input", "Build tools"};
    constexpr const char* values[] = {"0.1.0 / early preview", "x86, 32-bit protected mode", "800 x 600 / VBE", "PS/2 keyboard + mouse", "Python 3 + GNU C++17"};
    for (int i = 0; i < 5; ++i) {
        text(r.x + 26, r.y + 141 + i * 25, labels[i], muted);
        text(r.x + 154, r.y + 141 + i * 25, values[i], ink);
    }
    text(r.x + 26, r.y + 285, "Not Microsoft Windows. No EXE compatibility.", 0x8b744d);
    text(r.x + 26, r.y + 305, "Python runs on the host, not in the kernel.", muted);
}

void paint_window(int app) {
    const Rect r = windows[app].bounds;
    const bool active = active_app() == app;
    graphics::shadow(r);
    fill(r, active ? 0x1e59c7 : 0x799bce);
    outline(r, active ? 0x143b88 : 0x59779c);
    gradient({r.x + 2, r.y + 2, r.w - 4, 29}, active ? 0x4d92f1 : 0x9db9df, active ? 0x1d59cf : 0x7799c9);
    fill({r.x + 3, r.y + 31, r.w - 6, r.h - 34}, paper);
    icon(app, r.x + 9, r.y + 9, true);
    text(r.x + 31, r.y + 12, titles[app], 0xffffff);
    Rect minimize = {r.x + r.w - 51, r.y + 6, 20, 20};
    Rect close = {r.x + r.w - 27, r.y + 6, 20, 20};
    gradient(minimize, 0x8cb8f1, 0x3f77cf);
    outline(minimize, 0xc7ddf9);
    fill({minimize.x + 5, minimize.y + 13, 10, 2}, 0xffffff);
    gradient(close, close.contains(mouse_x, mouse_y) ? 0xffb09c : 0xe79b83, 0xb8462e);
    outline(close, 0xffd6be);
    for (int i = 0; i < 8; ++i) {
        fill({close.x + 6 + i, close.y + 6 + i, 2, 1}, 0xffffff);
        fill({close.x + 13 - i, close.y + 6 + i, 2, 1}, 0xffffff);
    }
    switch (app) {
        case Welcome: welcome_content(r); break;
        case Files: files_content(r); break;
        case Terminal: terminal_content(r); break;
        case Notes: notes_content(r); break;
        case System: system_content(r); break;
    }
}

int task_count() {
    int count = 0;
    for (const auto& window : windows) if (window.open) ++count;
    return count;
}
int task_width() { const int count = task_count(); return count ? min(148, 562 / count) : 148; }
void paint_taskbar() {
    gradient({0, taskbar_y, 800, 40}, 0x367dec, 0x1954bc);
    fill({0, taskbar_y, 800, 2}, 0x70a9f5);
    gradient({0, taskbar_y, start_width, 40}, start_open ? 0x307638 : 0x69ac48, 0x2a7c39);
    fill({0, taskbar_y + 1, start_width - 2, 1}, 0xa5d88a);
    flag(14, taskbar_y + 12, 8);
    text(42, taskbar_y + 13, "start", 0xffffff, 2);
    const int width = task_width();
    int count = 0;
    for (int app = 0; app < app_count; ++app) if (windows[app].open) {
        const Rect tab = {task_left + count++ * width, taskbar_y + 6, width - 5, 29};
        const bool active = active_app() == app && !windows[app].minimized;
        gradient(tab, active ? 0x1c4aa3 : 0x4b8bea, active ? 0x2860bd : 0x3776d9);
        outline(tab, active ? 0x183f89 : 0x79a9ef);
        icon(app, tab.x + 7, tab.y + 8, true);
        text_n(tab.x + 28, tab.y + 11, app_names[app], (tab.w - 34) / 8, 0xffffff);
    }
    gradient({713, taskbar_y + 2, 87, 38}, 0x27a5d6, 0x1485bc);
    fill({713, taskbar_y + 2, 1, 38}, 0x126fa7);
    circle(728, taskbar_y + 20, 3, 0xc2f3c8);
    const u32 seconds = hardware::seconds();
    const u32 minutes = (seconds / 60) % 100;
    char clock[6] = {static_cast<char>('0' + minutes / 10), static_cast<char>('0' + minutes % 10), ':', static_cast<char>('0' + (seconds % 60) / 10), static_cast<char>('0' + seconds % 10), '\0'};
    text(744, taskbar_y + 16, clock, 0xffffff);
}
void paint_start() {
    Rect menu = {4, 224, 352, 336};
    graphics::shadow(menu);
    fill(menu, 0xffffff);
    outline(menu, 0x154696);
    gradient({5, 225, 350, 57}, 0x4f93ed, 0x2562c1);
    fill({17, 237, 33, 32}, 0xe5f1fb);
    flag(23, 244, 10);
    text(64, 242, "Explorer", 0xffffff, 2);
    text(65, 264, "WELCOME TO XP / 96", 0xd5e8ff);
    fill({217, 283, 137, 232}, 0xe8eff9);
    constexpr const char* descriptions[] = {"A fresh start", "Built-in files", "Talk to the kernel", "A page of your own", "Under the hood"};
    for (int app = 0; app < app_count; ++app) {
        const Rect row = {9, 287 + app * 43, 203, 40};
        if (row.contains(mouse_x, mouse_y)) fill(row, 0xe2edfc);
        icon(app, 17, row.y + 7, true);
        text(43, row.y + 7, app_names[app], ink);
        text(43, row.y + 23, descriptions[app], muted);
    }
    text(231, 303, "YOUR SYSTEM", 0x305d96);
    fill({230, 324, 111, 1}, 0xc5d4ea);
    text(231, 343, "C++ kernel", 0x587390);
    text(231, 369, "Python tools", 0x587390);
    text(231, 395, "100% curious", 0x587390);
    text(231, 465, "PREVIEW 0.1", 0x8094b1);
    gradient({5, 516, 350, 43}, 0x3e85dc, 0x2562b8);
    text(19, 533, "YOUR OWN LITTLE WORLD.", 0xe0ecfc);
    button({257, 524, 88, 27}, "Restart");
}
void desktop_icons() {
    constexpr const char* labels[] = {"My computer", "Welcome", "Terminal", "Notes"};
    constexpr int apps[] = {System, Welcome, Terminal, Notes};
    for (int i = 0; i < 4; ++i) {
        const int y = 35 + i * 108;
        icon(apps[i], 36, y);
        const int x = 58 - length(labels[i]) * 4;
        text(x + 1, y + 51, labels[i], 0x38729a);
        text(x, y + 50, labels[i], 0xffffff);
    }
}

void click() {
    if (mouse_y >= taskbar_y) {
        if (mouse_x < start_width) { start_open = !start_open; dirty = true; return; }
        start_open = false;
        const int width = task_width();
        int count = 0;
        for (int app = 0; app < app_count; ++app) if (windows[app].open) {
            const Rect tab = {task_left + count++ * width, taskbar_y + 6, width - 5, 29};
            if (tab.contains(mouse_x, mouse_y)) {
                if (active_app() == app && !windows[app].minimized) windows[app].minimized = true;
                else { windows[app].minimized = false; focus(app); }
                dirty = true;
                return;
            }
        }
        dirty = true;
        return;
    }
    if (start_open) {
        if (Rect{257, 524, 88, 27}.contains(mouse_x, mouse_y)) hardware::reboot();
        for (int app = 0; app < app_count; ++app)
            if (Rect{9, 287 + app * 43, 203, 40}.contains(mouse_x, mouse_y)) { open_app(app); return; }
        if (!Rect{4, 224, 352, 336}.contains(mouse_x, mouse_y)) start_open = false;
        dirty = true;
        return;
    }
    for (int i = app_count - 1; i >= 0; --i) {
        const int app = order[i];
        const Rect r = windows[app].bounds;
        if (!windows[app].open || windows[app].minimized || !r.contains(mouse_x, mouse_y)) continue;
        focus(app);
        if (Rect{r.x + r.w - 27, r.y + 6, 20, 20}.contains(mouse_x, mouse_y)) { close_app(app); return; }
        if (Rect{r.x + r.w - 51, r.y + 6, 20, 20}.contains(mouse_x, mouse_y)) { windows[app].minimized = true; return; }
        if (mouse_y < r.y + 31) {
            dragging = app; drag_x = mouse_x - r.x; drag_y = mouse_y - r.y;
            return;
        }
        if (app == Welcome) {
            if (Rect{r.x + 25, r.y + 282, 240, 39}.contains(mouse_x, mouse_y)) open_app(Files);
            else if (Rect{r.x + 279, r.y + 282, 244, 39}.contains(mouse_x, mouse_y)) open_app(Terminal);
        }
        if (app == Files) {
            constexpr int targets[] = {Welcome, System, Notes};
            for (int row = 0; row < 3; ++row)
                if (Rect{r.x + 16, r.y + 113 + row * 53, r.w - 32, 47}.contains(mouse_x, mouse_y)) open_app(targets[row]);
        }
        return;
    }
    constexpr int targets[] = {Files, Welcome, Terminal, Notes};
    for (int i = 0; i < 4; ++i)
        if (Rect{8, 29 + i * 108, 108, 80}.contains(mouse_x, mouse_y)) { open_app(targets[i]); return; }
}

char translate(u8 scan) {
    static constexpr char normal[] = "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static constexpr char shifted[] = "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    if (scan >= sizeof(normal) - 1) return 0;
    const bool shift = shift_left || shift_right;
    char result = shift ? shifted[scan] : normal[scan];
    if (caps_lock && normal[scan] >= 'a' && normal[scan] <= 'z') result = shift ? normal[scan] : shifted[scan];
    return result;
}
}

namespace desktop {
void initialize() {
    notes_length = length(notes);
    terminal_line("XP / 96 [version 0.1.0]");
    terminal_line("Your kernel is running. Type help to explore.");
    last_second = hardware::seconds();
}
void render_if_needed() {
    const u32 now = hardware::seconds();
    if (now != last_second) { last_second = now; dirty = true; }
    if (!dirty) return;
    dirty = false;
    graphics::wallpaper();
    desktop_icons();
    for (int i = 0; i < app_count; ++i) {
        const int app = order[i];
        if (windows[app].open && !windows[app].minimized) paint_window(app);
    }
    paint_taskbar();
    if (start_open) paint_start();
    graphics::pointer(mouse_x, mouse_y);
    graphics::present();
}
void mouse(int dx, int dy, bool left) {
    mouse_x = clamp(mouse_x + dx, 0, 799);
    mouse_y = clamp(mouse_y + dy, 0, 599);
    if (left && !mouse_left) click();
    if (left && dragging >= 0) {
        auto& bounds = windows[dragging].bounds;
        bounds.x = clamp(mouse_x - drag_x, 0, 800 - bounds.w);
        bounds.y = clamp(mouse_y - drag_y, 0, taskbar_y - bounds.h);
    }
    if (!left) dragging = -1;
    mouse_left = left;
    if (dx || dy || left) dirty = true;
}
void key(u8 scan) {
    if (scan == 0xe0) { extended = true; return; }
    const bool was_extended = extended;
    extended = false;
    const bool released = scan & 0x80;
    scan &= 0x7f;
    if (scan == 0x2a && !was_extended) { shift_left = !released; return; }
    if (scan == 0x36 && !was_extended) { shift_right = !released; return; }
    if (scan == 0x1d) { control = !released; return; }
    if (scan == 0x38) { alt = !released; return; }
    if (released) return;
    if (scan == 0x3a) { caps_lock = !caps_lock; return; }
    if (was_extended && scan == 0x53 && control && alt) hardware::reboot();
    if (was_extended && (scan == 0x5b || scan == 0x5c)) { start_open = !start_open; dirty = true; return; }
    if (scan == 0x01) { start_open = false; dirty = true; return; }
    if (alt && scan == 0x3e) { close_app(active_app()); return; }
    if (scan >= 0x3b && scan <= 0x3f) { open_app(scan - 0x3b); return; }
    if (scan == 0x0f) {
        const int current = active_app();
        for (int next = 1; next <= app_count; ++next) {
            const int app = (current + next + app_count) % app_count;
            if (windows[app].open && !windows[app].minimized) { focus(app); break; }
        }
        return;
    }
    const int app = active_app();
    if (start_open) {
        if (scan == 0x1c) open_app(Welcome);
        return;
    }
    if (app == Terminal && was_extended && scan == 0x48) {
        copy_text(command, previous_command, sizeof(command));
        command_length = length(command);
        dirty = true;
        return;
    }
    if (was_extended) return;
    const char value = translate(scan);
    if (app == Terminal) {
        if (control && scan == 0x26) { terminal_count = 0; dirty = true; return; }
        if (control || alt) return;
        if (value == '\n') execute();
        else if (value == '\b') { if (command_length) command[--command_length] = '\0'; }
        else if (value >= 32 && value <= 126 && command_length < static_cast<int>(sizeof(command)) - 1) {
            command[command_length++] = value;
            command[command_length] = '\0';
        }
        dirty = true;
    } else if (app == Notes) {
        if (control && scan == 0x1f) { notes_warning = true; dirty = true; return; }
        if (control || alt) return;
        if (value == '\b') { if (notes_length) notes[--notes_length] = '\0'; }
        else if ((value == '\n' || (value >= 32 && value <= 126)) && notes_length < static_cast<int>(sizeof(notes)) - 1) {
            notes[notes_length++] = value;
            notes[notes_length] = '\0';
        }
        dirty = true;
    }
}
}
