// keyboard.cpp - MicroNT M18: PS/2 keyboard driver (IRQ 1)
#include "../../include/ntdef.h"
#include "../../include/hal.h"
#include "../../include/debug.h"

namespace KB {

// Scan-code set 1 -> ASCII (no shift).
// Index is the make-code (0x00-0x7F); 0 means ignored.
static const char s_scan[128] = {
//  0     1     2    3    4    5    6    7    8    9    A    B    C    D    E    F
    0,    0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b','\t',// 0x0
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',  0,  'a', 's', // 0x1
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';','\'', '`',  0, '\\','z', 'x', 'c', 'v', // 0x2
    'b', 'n', 'm', ',', '.', '/',  0,  '*',  0,  ' ',  0,   0,   0,   0,   0,   0, // 0x3
      0,   0,   0,   0,   0,   0,   0,  '7', '8', '9', '-', '4', '5', '6', '+', '1', // 0x4
    '2', '3', '0', '.',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 0x5
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 0x6
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 0x7
};

// static u8  s_ring[128];
// static u32 s_head = 0, s_tail = 0;
static u8           s_ring[128];
static volatile u32 s_head = 0;
static volatile u32 s_tail = 0;

static u8 Inb(u16 port) {
    u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v;
}

void HandleIrq(u8 /*irq*/) {
    u8 sc = Inb(0x60);
    if (sc & 0x80) return;          // break code (key release) -- ignore
    if (sc >= 128) return;
    char c = s_scan[sc];
    if (!c) return;
    u32 next = (s_tail + 1) & 127;
    if (next == s_head) return;     // buffer full
    s_ring[s_tail] = (u8)c;
    s_tail = next;
    // // Echo to VGA so the user sees what they type
    // VGA::PutChar(c, 0x0F);
    // Do not echo in IRQ context. Line editing and echoing are handled
    // by NtReadLine so input semantics stay console-owned.
}

bool TryRead(char* out) {
    if (s_head == s_tail) return false;
    *out = (char)s_ring[s_head];
    s_head = (s_head + 1) & 127;
    return true;
}

void Init() {
    s_head = s_tail = 0;
    // Drain any stale data in the PS/2 port
    while (Inb(0x64) & 1) (void)Inb(0x60);
    KDBG_INFO("KB: PS/2 keyboard driver initialized");
}

} // namespace KB
