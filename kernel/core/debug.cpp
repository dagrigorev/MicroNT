// debug.cpp - MicroNT debug output via COM1 serial

#include "../include/debug.h"
#include "../include/hal.h"

// ============================================================
// Printf implementation (no <stdio.h>)
// ============================================================
namespace {

static u16 s_port = COM1_PORT;

static void put(char c) {
    Serial::PutChar(s_port, c);
}

static void print_str(const char* s) {
    if (!s) s = "(null)";
    while (*s) put(*s++);
}

static void print_u64(u64 val, int base, bool upper, int width, char pad) {
    constexpr char digits_lo[] = "0123456789abcdef";
    constexpr char digits_hi[] = "0123456789ABCDEF";
    const char* digits = upper ? digits_hi : digits_lo;

    char buf[64];
    int  idx = 0;

    if (val == 0) {
        buf[idx++] = '0';
    } else {
        while (val) {
            buf[idx++] = digits[val % base];
            val /= base;
        }
    }

    // Padding
    while (idx < width) buf[idx++] = pad;

    // Reverse and emit
    for (int i = idx - 1; i >= 0; --i) put(buf[i]);
}

static void print_i64(i64 val, int width, char pad) {
    if (val < 0) { put('-'); val = -val; }
    print_u64(static_cast<u64>(val), 10, false, width, pad);
}

} // anonymous namespace

namespace Debug {

void Init() {
    s_port = COM1_PORT;
}

void PutChar(char c) {
    if (c == '\n') Serial::PutChar(s_port, '\r');
    Serial::PutChar(s_port, c);
}

void Print(const char* str) {
    while (*str) PutChar(*str++);
}

void Printf(const char* fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    for (const char* p = fmt; *p; ++p) {
        if (*p != '%') { PutChar(*p); continue; }

        ++p;
        int  width = 0;
        char pad   = ' ';

        if (*p == '0') { pad = '0'; ++p; }
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); ++p; }

        bool long_long = false;
        if (*p == 'l') {
            ++p;
            if (*p == 'l') { long_long = true; ++p; }
        }

        switch (*p) {
        case 'd': {
            i64 v = long_long ? __builtin_va_arg(args, i64)
                              : (i64)__builtin_va_arg(args, int);
            print_i64(v, width, pad);
            break;
        }
        case 'u': {
            u64 v = long_long ? __builtin_va_arg(args, u64)
                              : (u64)__builtin_va_arg(args, unsigned int);
            print_u64(v, 10, false, width, pad);
            break;
        }
        case 'x': {
            u64 v = long_long ? __builtin_va_arg(args, u64)
                              : (u64)__builtin_va_arg(args, unsigned int);
            print_u64(v, 16, false, width, pad);
            break;
        }
        case 'X': {
            u64 v = long_long ? __builtin_va_arg(args, u64)
                              : (u64)__builtin_va_arg(args, unsigned int);
            print_u64(v, 16, true, width, pad);
            break;
        }
        case 'p': {
            u64 v = (u64)(uptr)__builtin_va_arg(args, void*);
            put('0'); put('x');
            print_u64(v, 16, false, 16, '0');
            break;
        }
        case 's': {
            const char* s = __builtin_va_arg(args, const char*);
            print_str(s);
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(args, int);
            put(c);
            break;
        }
        case '%':
            put('%');
            break;
        default:
            put('%');
            put(*p);
            break;
        }
    }

    __builtin_va_end(args);
}

} // namespace Debug
