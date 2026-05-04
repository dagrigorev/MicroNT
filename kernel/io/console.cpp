// console.cpp + io_manager.cpp - MicroNT I/O manager (M1: serial console stub)

#include "../include/io.h"
#include "../include/debug.h"
#include "../include/hal.h"

// ============================================================
// I/O Manager
// ============================================================
namespace IO {

void Init() {
    // TODO(M4): Initialize device objects, I/O request infrastructure
    KDBG_INFO("IO: I/O manager initialized (M1 stub)");
}

// ============================================================
// Console (backed by COM1 serial for M1)
// ============================================================
namespace Console {

void Init() {
    KDBG_INFO("IO: Console initialized (serial-backed)");
}

void Write(const char* buf, usize len) {
    for (usize i = 0; i < len; ++i) {
        if (buf[i] == '\n') Serial::PutChar(COM1_PORT, '\r');
        Serial::PutChar(COM1_PORT, buf[i]);
    }
}

void WriteChar(char c) {
    if (c == '\n') Serial::PutChar(COM1_PORT, '\r');
    Serial::PutChar(COM1_PORT, c);
}

usize Read(char* buf, usize max_len) {
    usize n = 0;
    while (n < max_len - 1) {
        char c = Serial::GetChar(COM1_PORT);
        if (c == '\r' || c == '\n') {
            buf[n++] = '\n';
            break;
        }
        buf[n++] = c;
    }
    buf[n] = 0;
    return n;
}

} // namespace Console
} // namespace IO

