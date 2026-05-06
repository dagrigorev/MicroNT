// vga.cpp - MicroNT M17: VGA 80x25 text-mode console
#include "../../include/ntdef.h"

namespace VGA {

static constexpr u32 COLS = 80, ROWS = 25;
static volatile u16* const s_buf = reinterpret_cast<volatile u16*>(0xB8000);
static u32 s_row = 1;   // row 0 is reserved for the header bar
static u32 s_col = 0;

static void out8(u16 port, u8 val) {
    __asm__ volatile("outb %0,%1" :: "a"(val), "Nd"(port));
}

static void UpdateCursor() {
    u32 pos = s_row * COLS + s_col;
    out8(0x3D4, 0x0F); out8(0x3D5, (u8)(pos & 0xFF));
    out8(0x3D4, 0x0E); out8(0x3D5, (u8)(pos >> 8));
}

static void Scroll() {
    for (u32 r = 2; r < ROWS; ++r)
        for (u32 c = 0; c < COLS; ++c)
            s_buf[(r-1)*COLS+c] = s_buf[r*COLS+c];
    for (u32 c = 0; c < COLS; ++c)
        s_buf[(ROWS-1)*COLS+c] = 0x0720;
    s_row = ROWS - 1;
}

void PutChar(char ch, u8 attr) {
    if (ch == '\r') { s_col = 0; return; }
    if (ch == '\n') {
        s_col = 0;
        if (++s_row >= ROWS) Scroll();
        return;
    }
    s_buf[s_row * COLS + s_col] = ((u16)attr << 8) | (u8)ch;
    if (++s_col >= COLS) {
        s_col = 0;
        if (++s_row >= ROWS) Scroll();
    }
}

void Print(const char* s, u8 attr) {
    for (; *s; ++s) PutChar(*s, attr);
    UpdateCursor();
}

void Init() {
    // Clear screen (light-gray on black)
    for (u32 i = 0; i < ROWS * COLS; ++i) s_buf[i] = 0x0720;
    // Header bar: blue bg, bright white fg
    const char* hdr = " MicroNT Kernel ";
    u32 hlen = 0; while (hdr[hlen]) ++hlen;
    u32 hstart = (COLS - hlen) / 2;
    for (u32 i = 0; i < COLS; ++i) s_buf[i] = 0x1F20;
    for (u32 i = 0; i < hlen; ++i) s_buf[hstart + i] = 0x1F00 | (u8)hdr[i];
    s_row = 1; s_col = 0;
    UpdateCursor();
}

// Print a [USER] line in cyan
void PrintUser(const char* buf, usize len) {
    const char* prefix = "[USER] ";
    Print(prefix, 0x0B);     // cyan
    for (usize i = 0; i < len && buf[i] && buf[i] != '\r'; ++i) {
        if (buf[i] == '\n') break;
        PutChar(buf[i], 0x0F);
    }
    PutChar('\n', 0x07);
    UpdateCursor();
}

} // namespace VGA
