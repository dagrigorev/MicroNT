// keyboard.cpp - MicroNT M25: PS/2 keyboard driver
// Features: Shift, Caps Lock, extended key codes (arrows) via 0xE0 prefix
#include "../../include/ntdef.h"
#include "../../include/hal.h"
#include "../../include/debug.h"

namespace KB {

// ---------------------------------------------------------------------------
// Scan-code set 1 -- unshifted
// ---------------------------------------------------------------------------
static const char s_lo[128] = {
//  0     1     2    3    4    5    6    7    8    9    A    B    C    D    E    F
    0,    0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b','\t', // 0x0
   'q',  'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',  0,  'a', 's',  // 0x1
   'd',  'f', 'g', 'h', 'j', 'k', 'l', ';','\'', '`',  0, '\\','z',  'x', 'c', 'v',  // 0x2
   'b',  'n', 'm', ',', '.', '/',  0,  '*',  0,  ' ',  0,   0,   0,   0,   0,   0,   // 0x3
     0,   0,   0,   0,   0,   0,   0,  '7', '8', '9', '-', '4', '5', '6', '+', '1',  // 0x4
   '2',  '3', '0', '.',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x5
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x6
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x7
};

// ---------------------------------------------------------------------------
// Scan-code set 1 -- shifted (Shift held, or Caps Lock for letters)
// ---------------------------------------------------------------------------
static const char s_hi[128] = {
//  0     1     2    3    4    5    6    7    8    9    A    B    C    D    E    F
    0,    0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b','\t', // 0x0
   'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',  0,  'A', 'S',  // 0x1
   'D',  'F', 'G', 'H', 'J', 'K', 'L', ':','"',  '~',  0,  '|', 'Z', 'X', 'C', 'V',  // 0x2
   'B',  'N', 'M', '<', '>', '?',  0,  '*',  0,  ' ',  0,   0,   0,   0,   0,   0,   // 0x3
     0,   0,   0,   0,   0,   0,   0,  '7', '8', '9', '-', '4', '5', '6', '+', '1',  // 0x4
   '2',  '3', '0', '.',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x5
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x6
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x7
};

// Special codes stored in the ring buffer for extended keys.
// C0 control chars in 0x10-0x1F range that are unused in normal text input.
static constexpr u8 KEY_UP    = 0x10;  // DLE
static constexpr u8 KEY_DOWN  = 0x11;  // DC1
static constexpr u8 KEY_LEFT  = 0x12;  // DC2
static constexpr u8 KEY_RIGHT = 0x13;  // DC3

static u8           s_ring[128];
static volatile u32 s_head = 0;
static volatile u32 s_tail = 0;

static bool s_shift    = false;
static bool s_capslock = false;
static bool s_ext      = false;  // previous byte was 0xE0

static u8 Inb(u16 port) {
    u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v;
}

static void Push(u8 c) {
    u32 next = (s_tail + 1) & 127;
    if (next == s_head) return;  // buffer full
    s_ring[s_tail] = c;
    s_tail = next;
}

void HandleIrq(u8 /*irq*/) {
    u8 sc = Inb(0x60);

    // Extended-key prefix: next scancode is an extended key
    if (sc == 0xE0) { s_ext = true; return; }

    bool released = (sc & 0x80) != 0;
    u8   make     = sc & 0x7F;

    if (s_ext) {
        s_ext = false;
        if (!released) {
            if      (make == 0x48) Push(KEY_UP);
            else if (make == 0x50) Push(KEY_DOWN);
            else if (make == 0x4B) Push(KEY_LEFT);
            else if (make == 0x4D) Push(KEY_RIGHT);
        }
        return;
    }

    // Left Shift = 0x2A, Right Shift = 0x36
    if (make == 0x2A || make == 0x36) { s_shift = !released; return; }

    // Caps Lock = 0x3A -- toggle on press
    if (make == 0x3A && !released)    { s_capslock = !s_capslock; return; }

    if (released || make >= 128) return;

    // Caps Lock only affects letters; Shift affects everything
    char lo = s_lo[make];
    bool letter = (lo >= 'a' && lo <= 'z');
    bool use_hi = letter ? (s_shift != s_capslock) : s_shift;

    char c = use_hi ? s_hi[make] : s_lo[make];
    if (c) Push((u8)c);
}

bool TryRead(char* out) {
    if (s_head == s_tail) return false;
    *out = (char)s_ring[s_head];
    s_head = (s_head + 1) & 127;
    return true;
}

void Init() {
    s_head = s_tail = 0;
    s_shift = s_capslock = s_ext = false;
    while (Inb(0x64) & 1) (void)Inb(0x60);  // drain stale PS/2 data
    KDBG_INFO("KB: PS/2 keyboard driver initialized (M25: shift + arrows)");
}

} // namespace KB
