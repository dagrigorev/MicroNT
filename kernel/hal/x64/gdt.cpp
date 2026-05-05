// gdt.cpp - MicroNT 64-bit GDT + TSS setup
//
// GDT layout (required for SYSCALL/SYSRET):
//   [0] 0x00  null
//   [1] 0x08  kernel code (DPL=0, 64-bit)
//   [2] 0x10  kernel data (DPL=0)
//   [3] 0x18  user data   (DPL=3)   <- NOTE: data before code for SYSRET
//   [4] 0x20  user code   (DPL=3, 64-bit)
//   [5] 0x28  TSS descriptor (16 bytes)
//
// STAR MSR: STAR[63:48]=0x10  STAR[47:32]=0x08
//   SYSCALL -> CS=0x08 SS=0x10
//   SYSRET  -> CS=0x10+16|3=0x23  SS=0x10+8|3=0x1B

#include "../../include/hal.h"
#include "../../include/debug.h"

// ============================================================
// Structures
// ============================================================
struct GdtEntry {
    u16 LimitLow, BaseLow;
    u8  BaseMiddle, Access, Granularity, BaseHigh;
} __attribute__((packed));

struct GdtEntry16 {
    GdtEntry Low;
    u32      BaseUpper;
    u32      Reserved;
} __attribute__((packed));

struct GdtPointer {
    u16 Limit;
    u64 Base;
} __attribute__((packed));

struct Tss64 {
    u32 Reserved0;
    u64 Rsp0, Rsp1, Rsp2;
    u64 Reserved1;
    u64 Ist[7];
    u64 Reserved2;
    u16 Reserved3;
    u16 IoMapBase;
} __attribute__((packed));

struct GdtTable {
    GdtEntry   entries[5];   // 0x00-0x27 (40 bytes)
    GdtEntry16 tss_desc;     // 0x28 (16 bytes) = selector 0x28
} __attribute__((packed));

// ============================================================
// Storage
// ============================================================
alignas(16) static u8       s_kernel_stack[16384];
alignas(16) static GdtTable s_gdt_table;
alignas(16) static Tss64    s_tss;
static GdtPointer           s_gdt_ptr;

// Cached RSP0 value - read by syscall_entry.asm
// Updated by SetTSSRsp0() on every context switch.
u64 g_kernel_rsp0 = 0;

// ============================================================
// Helpers
// ============================================================
static void SetEntry(GdtEntry& e, u32 base, u32 limit,
                     u8 access, u8 granularity) {
    e.BaseLow     = base & 0xFFFF;
    e.BaseMiddle  = (base >> 16) & 0xFF;
    e.BaseHigh    = (base >> 24) & 0xFF;
    e.LimitLow    = limit & 0xFFFF;
    e.Granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    e.Access      = access;
}

namespace HAL {

void GdtInit() {
    SetEntry(s_gdt_table.entries[0], 0, 0, 0, 0);              // 0x00 null
    SetEntry(s_gdt_table.entries[1], 0, 0xFFFFF, 0x9A, 0xAF);  // 0x08 kernel code
    SetEntry(s_gdt_table.entries[2], 0, 0xFFFFF, 0x92, 0xAF);  // 0x10 kernel data
    SetEntry(s_gdt_table.entries[3], 0, 0xFFFFF, 0xF2, 0xAF);  // 0x18 user data (DPL=3)
    SetEntry(s_gdt_table.entries[4], 0, 0xFFFFF, 0xFA, 0xAF);  // 0x20 user code (DPL=3)

    // TSS at offset 0x28
    u64 tss_base = reinterpret_cast<u64>(&s_tss);
    u32 tss_size = static_cast<u32>(sizeof(Tss64) - 1);

    s_gdt_table.tss_desc.Low.LimitLow    = tss_size & 0xFFFF;
    s_gdt_table.tss_desc.Low.BaseLow     = tss_base & 0xFFFF;
    s_gdt_table.tss_desc.Low.BaseMiddle  = (tss_base >> 16) & 0xFF;
    s_gdt_table.tss_desc.Low.Access      = 0x89;   // Present, TSS64 Available, DPL=0
    s_gdt_table.tss_desc.Low.Granularity = (tss_size >> 16) & 0x0F;
    s_gdt_table.tss_desc.Low.BaseHigh    = (tss_base >> 24) & 0xFF;
    s_gdt_table.tss_desc.BaseUpper       = (tss_base >> 32) & 0xFFFFFFFF;
    s_gdt_table.tss_desc.Reserved        = 0;

    s_tss.Rsp0      = reinterpret_cast<u64>(s_kernel_stack + sizeof(s_kernel_stack));
    s_tss.IoMapBase = static_cast<u16>(sizeof(Tss64));
    g_kernel_rsp0   = s_tss.Rsp0;

    s_gdt_ptr.Limit = static_cast<u16>(sizeof(GdtTable) - 1);
    s_gdt_ptr.Base  = reinterpret_cast<u64>(&s_gdt_table);

    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :: "m"(s_gdt_ptr) : "rax", "memory"
    );

    __asm__ volatile("ltr %0" :: "rm"((u16)0x28));
}

void SetTSSRsp0(u64 rsp0) {
    s_tss.Rsp0  = rsp0;
    g_kernel_rsp0 = rsp0;   // keep assembly-visible cache in sync
}

} // namespace HAL
