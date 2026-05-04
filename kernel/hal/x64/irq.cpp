// irq.cpp - MicroNT IRQ dispatch table (IRQ0-IRQ15)

#include "../../include/hal.h"
#include "../../include/debug.h"
#include "../../include/spinlock.h"

namespace {

static IrqHandler s_handlers[16] = {};
static Spinlock   s_lock;

// Default handler: just log and EOI
// Matches HAL::IrqHandler = void(*)(u8)
static void DefaultHandler(u8 irq) {
    KDBG_WARN("IRQ%u: no handler registered", (u32)irq);
}

} // namespace

namespace HAL {

void IrqInit() {
    for (u8 i = 0; i < 16; ++i)
        s_handlers[i] = DefaultHandler;
}

void IrqRegister(u8 irq, IrqHandler handler) {
    if (irq >= 16) return;
    SpinlockGuard g(s_lock);
    s_handlers[irq] = handler ? handler : DefaultHandler;
}

void IrqUnregister(u8 irq) {
    if (irq >= 16) return;
    SpinlockGuard g(s_lock);
    s_handlers[irq] = DefaultHandler;
}

void IrqDispatch(u8 irq) {
    if (irq >= 16) return;

    // Call the registered handler
    s_handlers[irq](irq);

    // Send EOI to PIC (must come after handler so spurious IRQs are handled)
    PicSendEoi(irq);
}

} // namespace HAL
