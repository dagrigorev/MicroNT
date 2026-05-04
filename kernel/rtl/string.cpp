// string.cpp - MicroNT freestanding string/memory utilities
// No libc dependency.

#include "../include/ntdef.h"

// ============================================================
// Memory operations (required by compiler-generated code)
// ============================================================
extern "C" {

void* memset(void* dst, int val, usize len) {
    auto* p = static_cast<u8*>(dst);
    while (len--) *p++ = static_cast<u8>(val);
    return dst;
}

void* memcpy(void* dst, const void* src, usize len) {
    auto* d = static_cast<u8*>(dst);
    const auto* s = static_cast<const u8*>(src);
    while (len--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, usize len) {
    auto* d = static_cast<u8*>(dst);
    const auto* s = static_cast<const u8*>(src);
    if (d < s) {
        while (len--) *d++ = *s++;
    } else if (d > s) {
        d += len; s += len;
        while (len--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void* a, const void* b, usize len) {
    const auto* pa = static_cast<const u8*>(a);
    const auto* pb = static_cast<const u8*>(b);
    while (len--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        ++pa; ++pb;
    }
    return 0;
}

} // extern "C"

// ============================================================
// String operations
// ============================================================
extern "C" {

usize strlen(const char* s) {
    usize n = 0;
    while (s[n]) ++n;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, usize n) {
    while (n-- && *a && *a == *b) { ++a; ++b; }
    if (n == (usize)-1) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

char* strncpy(char* dst, const char* src, usize n) {
    char* d = dst;
    while (n && (*d++ = *src++)) --n;
    while (n--) *d++ = '\0';
    return dst;
}

const char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return s;
        ++s;
    }
    return (c == 0) ? s : nullptr;
}

} // extern "C"
