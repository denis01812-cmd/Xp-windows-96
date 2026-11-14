#include "types.hpp"

extern "C" void* memset(void* destination, int value, usize count) {
    auto* output = static_cast<u8*>(destination);
    for (usize i = 0; i < count; ++i) output[i] = static_cast<u8>(value);
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, usize count) {
    auto* output = static_cast<u8*>(destination);
    const auto* input = static_cast<const u8*>(source);
    for (usize i = 0; i < count; ++i) output[i] = input[i];
    return destination;
}

extern "C" void* memmove(void* destination, const void* source, usize count) {
    auto* output = static_cast<u8*>(destination);
    const auto* input = static_cast<const u8*>(source);
    if (reinterpret_cast<usize>(output) <= reinterpret_cast<usize>(input)) {
        for (usize i = 0; i < count; ++i) output[i] = input[i];
    } else {
        while (count) { --count; output[count] = input[count]; }
    }
    return destination;
}

int length(const char* text) {
    int size = 0;
    while (text[size]) ++size;
    return size;
}

bool equal(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

bool starts_with(const char* text, const char* prefix) {
    while (*prefix) if (*text++ != *prefix++) return false;
    return true;
}

void copy_text(char* destination, const char* source, int capacity) {
    if (capacity <= 0) return;
    int i = 0;
    for (; i < capacity - 1 && source[i]; ++i) destination[i] = source[i];
    destination[i] = '\0';
}

void decimal(u32 value, char* output) {
    char reversed[11];
    int size = 0;
    do { reversed[size++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    for (int i = 0; i < size; ++i) output[i] = reversed[size - i - 1];
    output[size] = '\0';
}
