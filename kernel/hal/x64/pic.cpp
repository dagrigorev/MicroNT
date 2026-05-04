// pic.cpp - MicroNT 8259A PIC initialization and EOI

#include "../../include/hal.h"
#include "../../include/debug.h"

// 8259A PIC ports
constexpr u16 PIC1_CMD  = 0x20;
constexpr u16 PIC1_DATA = 0x21;
constexpr u16 PIC2_CMD  = 0xA0;
constexpr u16 PIC2_DATA = 0xA1;

// ICW1 flags
constexpr u8 ICW1_ICW4   = 0x01;
[[maybe_unused]] constexpr u8 ICW1_SINGLE = 0x02;
constexpr u8 ICW1_INIT   = 0x10;

// ICW4 flags
constexpr u8 ICW4_8086   = 0x01;

// IRQ vector base
constexpr u8 IRQ0_VECTOR = 0x20;  // IRQ0-7  → INT 0x20-0x27
constexpr u8 IRQ8_VECTOR = 0x28;  // IRQ8-15 → INT 0x28-0x2F

namespace HAL {

void PicInit() {
    // Start initialization sequence (ICW1)
    IoOutByte(PIC1_CMD,  ICW1_INIT | ICW1_ICW4);
    IoWait();
    IoOutByte(PIC2_CMD,  ICW1_INIT | ICW1_ICW4);
    IoWait();

    // ICW2: vector offsets
    IoOutByte(PIC1_DATA, IRQ0_VECTOR);
    IoWait();
    IoOutByte(PIC2_DATA, IRQ8_VECTOR);
    IoWait();

    // ICW3: cascade identity
    IoOutByte(PIC1_DATA, 0x04);   // PIC1 has slave on IRQ2
    IoWait();
    IoOutByte(PIC2_DATA, 0x02);   // PIC2 identity = 2
    IoWait();

    // ICW4: 8086 mode
    IoOutByte(PIC1_DATA, ICW4_8086);
    IoWait();
    IoOutByte(PIC2_DATA, ICW4_8086);
    IoWait();

    // Mask all IRQs except cascade (IRQ2)
    IoOutByte(PIC1_DATA, 0xFB);  // allow IRQ2 only
    IoOutByte(PIC2_DATA, 0xFF);  // all masked
}

void PicSendEoi(u8 irq) {
    if (irq >= 8) IoOutByte(PIC2_CMD, 0x20);
    IoOutByte(PIC1_CMD, 0x20);
}

void PicSetMask(u8 irq, bool masked) {
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    u8  bit  = 1 << (irq & 7);
    u8  val  = IoInByte(port);
    if (masked) val |= bit; else val &= ~bit;
    IoOutByte(port, val);
}

} // namespace HAL
