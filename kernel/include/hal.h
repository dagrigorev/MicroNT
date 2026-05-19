#pragma once
// hal.h - MicroNT Hardware Abstraction Layer interface

#include "desktopmodel.h"
#include "ntdef.h"
#include "uxtheme.h"

// ============================================================
// Port I/O
// ============================================================
namespace HAL {

inline u8 IoInByte(u16 port) {
    u8 val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

inline void IoOutByte(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

inline u16 IoInWord(u16 port) {
    u16 val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

inline void IoOutWord(u16 port, u16 val) {
    __asm__ volatile("outw %0, %1" :: "a"(val), "Nd"(port));
}

inline u32 IoInDword(u16 port) {
    u32 val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

inline void IoOutDword(u16 port, u32 val) {
    __asm__ volatile("outl %0, %1" :: "a"(val), "Nd"(port));
}

inline void IoWait() {
    IoOutByte(0x80, 0);
}

// ============================================================
// MSR
// ============================================================
inline u64 ReadMsr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (static_cast<u64>(hi) << 32) | lo;
}

inline void WriteMsr(u32 msr, u64 value) {
    __asm__ volatile("wrmsr" ::
        "a"(static_cast<u32>(value)),
        "d"(static_cast<u32>(value >> 32)),
        "c"(msr));
}

// ============================================================
// CPU control
// ============================================================
[[noreturn]] inline void CpuHalt() {
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

inline void EnableInterrupts()  { __asm__ volatile("sti"); }
inline void DisableInterrupts() { __asm__ volatile("cli"); }

inline u64 ReadCr2() {
    u64 val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

inline u64 ReadCr3() {
    u64 val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

// ============================================================
// CPU detection
// ============================================================
void CpuDetect();
void GdtInit();
void IdtInit();
void PicInit();
void PicSendEoi(u8 irq);
void PicSetMask(u8 irq, bool masked);
void SetTSSRsp0(u64 rsp0);   // update TSS.RSP0 (kernel stack for ring-3 -> ring-0)
} // namespace HAL

// Global: current TSS.RSP0 value. Read by syscall_entry.asm (no SWAPGS needed).
extern u64 g_kernel_rsp0;

namespace HAL {

} // namespace HAL

// ============================================================
// IRQ handler type (outside HAL namespace so it's visible without qualification)
// ============================================================
using IrqHandler = void(*)(u8 irq);

namespace HAL {
void IrqInit();
void IrqRegister(u8 irq, IrqHandler handler);
void IrqUnregister(u8 irq);
void IrqDispatch(u8 irq);   // called from IDT for vectors 0x20-0x2F

void PitInit(u32 hz = 100);
u64  PitTicks();
void PitSleep(u32 ms);      // busy-wait using tick counter
} // namespace HAL

// ============================================================
// Serial constants
// ============================================================
constexpr u16 COM1_PORT    = 0x3F8;
constexpr u32 DEFAULT_BAUD = 115200;

namespace Serial {
bool Init(u16 port = COM1_PORT, u32 baud = DEFAULT_BAUD);
void PutChar(u16 port, char c);
char GetChar(u16 port);
void Print(u16 port, const char* str);
} // namespace Serial

// ============================================================
// VGA - GOP framebuffer console  (M24+)
// ============================================================
namespace VGA {
    struct FramebufferInfo {
        u32 Width;
        u32 Height;
        u32 Stride;
        u32 Format;
    };

    void SetFramebuffer(u64 base, u32 w, u32 h, u32 stride, u32 fmt);
    bool GetFramebufferInfo(FramebufferInfo& info);
    void Init();
    void StartDesktop(const UXTHEME::Theme& theme,
                      const DESKTOPMODEL::DesktopLayout& layout);
    void ClearScreen();        // fast clear rows 1+, keeps header bar
    void WriteWelcome();
    void Print(const char* s, u8 attr = 0x07);
    void PutChar(char ch, u8 attr = 0x07);
    void PrintUser(const char* buf, usize len);
    void UpdateCursor();       // show cursor at current position
    void BlinkCursor();        // toggle cursor blink (~every 50 PIT ticks)
    void UpdateStatusBar(u64 ticks); // update uptime + bottom hint bar (~every 100 ticks)
}

// ============================================================
// KB - PS/2 keyboard driver  (M18)
// ============================================================
namespace KB {
    void Init();
    bool TryRead(char* out);   // non-blocking; returns false if buffer empty
    void HandleIrq(u8 irq);
}
