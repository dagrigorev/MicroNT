// idt.cpp - MicroNT IDT setup and interrupt dispatch

#include "../../include/hal.h"
#include "../../include/debug.h"
#include "../../include/ntdef.h"

// ============================================================
// Interrupt frame (must match interrupts.asm push order)
// ============================================================
struct InterruptFrame {
    // Saved GPRs (PUSHAQ order, reversed)
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rsi, rdi, rdx, rcx, rbx, rax;
    // Pushed by ISR stub
    u64 Vector;
    u64 ErrorCode;
    // CPU-pushed on interrupt
    u64 Rip, Cs, Rflags, Rsp, Ss;
};

// ============================================================
// IDT entry
// ============================================================
struct IdtEntry {
    u16 OffsetLow;
    u16 Selector;
    u8  Ist;
    u8  TypeAttr;
    u16 OffsetMiddle;
    u32 OffsetHigh;
    u32 Reserved;
} __attribute__((packed));

struct IdtPointer {
    u16 Limit;
    u64 Base;
} __attribute__((packed));

static IdtEntry  s_idt[256];
static IdtPointer s_idt_ptr;

// Declare all ISR symbols
#define ISR_DECL(n) extern "C" void isr##n();
ISR_DECL(0)  ISR_DECL(1)  ISR_DECL(2)  ISR_DECL(3)
ISR_DECL(4)  ISR_DECL(5)  ISR_DECL(6)  ISR_DECL(7)
ISR_DECL(8)  ISR_DECL(9)  ISR_DECL(10) ISR_DECL(11)
ISR_DECL(12) ISR_DECL(13) ISR_DECL(14) ISR_DECL(15)
ISR_DECL(16) ISR_DECL(17) ISR_DECL(18) ISR_DECL(19)
ISR_DECL(20) ISR_DECL(21) ISR_DECL(22) ISR_DECL(23)
ISR_DECL(24) ISR_DECL(25) ISR_DECL(26) ISR_DECL(27)
ISR_DECL(28) ISR_DECL(29) ISR_DECL(30) ISR_DECL(31)
ISR_DECL(32) ISR_DECL(33) ISR_DECL(34) ISR_DECL(35)
ISR_DECL(36) ISR_DECL(37) ISR_DECL(38) ISR_DECL(39)
ISR_DECL(40) ISR_DECL(41) ISR_DECL(42) ISR_DECL(43)
ISR_DECL(44) ISR_DECL(45) ISR_DECL(46) ISR_DECL(47)

using IsrFn = void(*)();
static const IsrFn s_isrs[48] = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
    isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39,
    isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47,
};

static void SetGate(u8 n, void* handler, u8 type_attr) {
    u64 addr = reinterpret_cast<u64>(handler);
    s_idt[n].OffsetLow    = addr & 0xFFFF;
    s_idt[n].Selector     = 0x08;     // kernel code
    s_idt[n].Ist          = 0;
    s_idt[n].TypeAttr     = type_attr;
    s_idt[n].OffsetMiddle = (addr >> 16) & 0xFFFF;
    s_idt[n].OffsetHigh   = (addr >> 32) & 0xFFFFFFFF;
    s_idt[n].Reserved     = 0;
}

static const char* ExceptionName(u64 v) {
    constexpr const char* names[] = {
        "Divide Error",         "Debug",
        "NMI",                  "Breakpoint",
        "Overflow",             "Bound Range",
        "Invalid Opcode",       "No Math Coprocessor",
        "Double Fault",         "Coprocessor Segment",
        "Invalid TSS",          "Segment Not Present",
        "Stack Fault",          "General Protection",
        "Page Fault",           "Reserved",
        "Math Fault",           "Alignment Check",
        "Machine Check",        "SIMD FP",
    };
    if (v < 20) return names[v];
    return "Exception";
}

// ============================================================
// Interrupt dispatch (called from interrupts.asm)
// ============================================================
extern "C" void InterruptDispatch(InterruptFrame* frame) {
    u64 vec = frame->Vector;

    if (vec < 32) {
        // CPU exception
        if (vec == 14) {
            // Page fault
            u64 cr2 = HAL::ReadCr2();

            // Give VMM a chance to handle it (demand-zero, COW, etc.)
            // Include VMM header inline to avoid circular dep
            extern bool VmmHandlePageFault(u64 cr2, u32 error_code);
            if (VmmHandlePageFault(cr2, (u32)frame->ErrorCode)) {
                return;  // fault resolved, resume execution
            }

            // Unhandled - print diagnostics then panic
            Debug::Printf("\n[EXCEPTION] #PF at RIP=0x%016llx CR2=0x%016llx err=0x%llx\n",
                frame->Rip, cr2, frame->ErrorCode);
            Debug::Printf("  %s%s%s\n",
                (frame->ErrorCode & 1) ? "protection " : "not-present ",
                (frame->ErrorCode & 2) ? "write " : "read ",
                (frame->ErrorCode & 4) ? "user " : "kernel ");
        } else {
            Debug::Printf("\n[EXCEPTION] %s (#%llu) at RIP=0x%016llx err=0x%llx\n",
                ExceptionName(vec), vec, frame->Rip, frame->ErrorCode);
        }
        Debug::Printf("  RAX=0x%016llx RBX=0x%016llx RCX=0x%016llx\n",
            frame->rax, frame->rbx, frame->rcx);
        Debug::Printf("  RDX=0x%016llx RSI=0x%016llx RDI=0x%016llx\n",
            frame->rdx, frame->rsi, frame->rdi);
        Debug::Printf("  RSP=0x%016llx RBP=0x%016llx RFLAGS=0x%llx\n",
            frame->Rsp, frame->rbp, frame->Rflags);

        KernelPanic("Unhandled CPU exception");
    } else if (vec < 48) {
        // IRQ 0x20-0x2F -> IRQ line 0-15
        // IrqDispatch calls the registered handler then sends PIC EOI.
        HAL::IrqDispatch(static_cast<u8>(vec - 32));
    }
}

namespace HAL {

void IdtInit() {
    constexpr u8 INT_GATE = 0x8E;  // present, DPL0, 64-bit interrupt gate

    for (u8 i = 0; i < 48; ++i) {
        SetGate(i, reinterpret_cast<void*>(s_isrs[i]), INT_GATE);
    }

    s_idt_ptr.Limit = sizeof(s_idt) - 1;
    s_idt_ptr.Base  = reinterpret_cast<u64>(s_idt);

    __asm__ volatile("lidt %0" :: "m"(s_idt_ptr) : "memory");
}

} // namespace HAL
