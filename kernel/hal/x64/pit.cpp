// pit.cpp - MicroNT 8254 PIT driver
// Channel 0, mode 2 (rate generator), IRQ0

#include "../../include/hal.h"
#include "../../include/debug.h"
#include "../../include/process.h"

// 8254 port addresses
constexpr u16 PIT_CHAN0  = 0x40;   // Channel 0 data (read/write)
constexpr u16 PIT_CMD   = 0x43;   // Mode/command register (write only)

// Command word: channel 0, lo/hi byte access, mode 2 (rate generator)
constexpr u8  PIT_CMD_CHAN0_RATE = 0x34;  // 0b00_11_010_0

// Base oscillator frequency (Hz)
constexpr u32 PIT_BASE_HZ = 1193182;

namespace {

static volatile u64 s_ticks   = 0;
static          u32 s_hz      = 0;
static          u32 s_ms_per_tick = 0;  // 1000 / hz (rounded)

// IRQ0 handler - called from IrqDispatch
static void TimerHandler(u8 /*irq*/) {
    s_ticks = s_ticks + 1u;  // avoid deprecated volatile increment in C++20
    if ((s_ticks % 50)  == 0) VGA::BlinkCursor();            // ~500 ms blink
    if ((s_ticks % 100) == 0) VGA::UpdateStatusBar(s_ticks); // ~1 s uptime tick
    Sched::Tick();
}

} // namespace

namespace HAL {

void PitInit(u32 hz) {
    if (hz == 0) hz = 100;
    s_hz = hz;
    s_ms_per_tick = 1000 / hz;  // e.g. 10 ms at 100 Hz

    // Compute reload value (divisor)
    u32 divisor = PIT_BASE_HZ / hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    if (divisor < 1)      divisor = 1;

    // Program PIT channel 0: rate generator, lo/hi byte
    IoOutByte(PIT_CMD, PIT_CMD_CHAN0_RATE);
    IoOutByte(PIT_CHAN0, static_cast<u8>(divisor & 0xFF));
    IoOutByte(PIT_CHAN0, static_cast<u8>(divisor >> 8));

    // Register IRQ0 handler and unmask it in the PIC
    IrqRegister(0, TimerHandler);
    PicSetMask(0, false);  // unmask IRQ0

    KDBG_INFO("PIT: channel 0 at %u Hz (divisor=%u)", hz, divisor);
}

u64 PitTicks() {
    return s_ticks;
}

void PitSleep(u32 ms) {
    if (s_hz == 0) {
        // PIT not initialized - dumb spin
        // Busy-spin using asm to avoid deprecated volatile increment (C++20)
        u32 iters = ms * 50000u;
        for (u32 i = 0; i < iters; ++i)
            __asm__ volatile("pause" ::: "memory");
        return;
    }

    // Calculate how many ticks to wait (round up)
    u64 ticks_needed = ((u64)ms * s_hz + 999) / 1000;
    if (ticks_needed == 0) ticks_needed = 1;

    u64 start = s_ticks;
    while (s_ticks - start < ticks_needed) {
        __asm__ volatile("pause");
    }
}

} // namespace HAL
