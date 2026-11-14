#pragma once

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using i32 = int;
using usize = unsigned int;
static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4);
static_assert(sizeof(void*) == 4, "Build the kernel for 32-bit x86");

constexpr int clamp(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}
constexpr int min(int a, int b) { return a < b ? a : b; }
int length(const char* text);
bool equal(const char* a, const char* b);
bool starts_with(const char* text, const char* prefix);
void copy_text(char* destination, const char* source, int capacity);
void decimal(u32 value, char* output);

extern "C" void* memset(void* destination, int value, usize count);
extern "C" void* memcpy(void* destination, const void* source, usize count);
extern "C" void* memmove(void* destination, const void* source, usize count);
