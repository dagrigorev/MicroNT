// serial.cpp - MicroNT COM1 serial driver (115200 8N1)

#include "../../include/hal.h"

namespace Serial {

bool Init(u16 port, u32 baud) {
    using namespace HAL;

    u16 divisor = static_cast<u16>(115200 / baud);

    IoOutByte(port + 1, 0x00);  // disable interrupts
    IoOutByte(port + 3, 0x80);  // DLAB on
    IoOutByte(port + 0, static_cast<u8>(divisor & 0xFF));
    IoOutByte(port + 1, static_cast<u8>(divisor >> 8));
    IoOutByte(port + 3, 0x03);  // 8 bits, no parity, 1 stop, DLAB off
    IoOutByte(port + 2, 0xC7);  // FIFO on, clear, 14-byte threshold
    IoOutByte(port + 4, 0x0B);  // DTR + RTS + OUT2  (normal mode - set FIRST)

    // NOTE: do NOT do a loopback test here.
    // The loopback test (MCR=0x1E) leaves the UART in loopback mode if
    // the echo check fails (which happens when UEFI leaves COM1 in an
    // indeterminate state). Any byte written while MCR has bit 4 set
    // goes to the internal receive FIFO, not to the physical TX line.
    // VirtualBox would capture nothing and the kernel appears silent.
    return true;
}

static bool TxReady(u16 port) {
    // LSR bit 5 = Transmitter Holding Register Empty
    return (HAL::IoInByte(port + 5) & 0x20) != 0;
}

void PutChar(u16 port, char c) {
    // Poll with a finite timeout so we never hang if UART is in a bad state.
    // If THRE never sets, we write anyway (byte may be lost but we don't block).
    for (int i = 0; i < 1000000; ++i) {
        if (TxReady(port)) break;
    }
    HAL::IoOutByte(port, static_cast<u8>(c));
}

char GetChar(u16 port) {
    while ((HAL::IoInByte(port + 5) & 1) == 0);
    return static_cast<char>(HAL::IoInByte(port));
}

void Print(u16 port, const char* str) {
    while (*str) {
        if (*str == '\n') PutChar(port, '\r');
        PutChar(port, *str++);
    }
}

} // namespace Serial
