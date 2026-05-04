// gdt.cpp - MicroNT 64-bit GDT + TSS setup

#include "../../include/hal.h"
#include "../../include/debug.h"

// ============================================================
// GDT entry structures
// ============================================================
struct GdtEntry {
    u16 LimitLow;
    u16 BaseLow;
    u8  BaseMiddle;
    u8  Access;
    u8  Granularity;
    u8  BaseHigh;
} __attribute__((packed));

// 16-byte system descriptor (TSS / LDT in 64-bit mode)
struct GdtEntry16 {
    GdtEntry Low;
    u32      BaseUpper;
    u32      Reserved;
} __attribute__((packed));

struct GdtPointer {
    u16 Limit;
    u64 Base;
} __attribute__((packed));

// ============================================================
// TSS (minimal 64-bit)
// ============================================================
struct Tss64 {
    u32 Reserved0;
    u64 Rsp0, Rsp1, Rsp2;
    u64 Reserved1;
    u64 Ist[7];
    u64 Reserved2;
    u16 Reserved3;
    u16 IoMapBase;
} __attribute__((packed));

// ============================================================
// GDT table layout
//
// IMPORTANT: s_gdt_table must be a single contiguous struct so
// the TSS descriptor (at byte offset 40) immediately follows
// the 5 standard entries (5 * 8 = 40 bytes).
//
// Using separate variables with alignas(16) creates 8 bytes of
// padding between s_gdt[5] (40 bytes, not a multiple of 16)
// and the next alignas(16) variable, which puts the TSS at
// offset 48 instead of 40. The CPU reads garbage at offset 40
// when ltr 0x28 is executed, causing a #GP.
// ============================================================
struct GdtTable {
    GdtEntry  entries[5];  // null, kcode, kdata, ucode, udata  (40 bytes)
    GdtEntry16 tss_desc;   // TSS descriptor at offset 40 = selector 0x28 (16 bytes)
} __attribute__((packed));

// ============================================================
// Static storage
// ============================================================
alignas(16) static u8       s_kernel_stack[16384];
alignas(16) static GdtTable s_gdt_table;
alignas(16) static Tss64    s_tss;
static GdtPointer           s_gdt_ptr;

// ============================================================
// Helper: fill a standard 8-byte descriptor
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
    // ---- Standard descriptors ----
    SetEntry(s_gdt_table.entries[0], 0, 0, 0, 0);              // 0x00 null
    SetEntry(s_gdt_table.entries[1], 0, 0xFFFFF, 0x9A, 0xAF);  // 0x08 kernel code (64-bit)
    SetEntry(s_gdt_table.entries[2], 0, 0xFFFFF, 0x92, 0xAF);  // 0x10 kernel data
    SetEntry(s_gdt_table.entries[3], 0, 0xFFFFF, 0xFA, 0xAF);  // 0x18 user code (64-bit)
    SetEntry(s_gdt_table.entries[4], 0, 0xFFFFF, 0xF2, 0xAF);  // 0x20 user data

    // ---- TSS descriptor at offset 40 = selector 0x28 ----
    u64 tss_base = reinterpret_cast<u64>(&s_tss);
    u32 tss_size = static_cast<u32>(sizeof(Tss64) - 1);

    s_gdt_table.tss_desc.Low.LimitLow   = tss_size & 0xFFFF;
    s_gdt_table.tss_desc.Low.BaseLow    = tss_base & 0xFFFF;
    s_gdt_table.tss_desc.Low.BaseMiddle = (tss_base >> 16) & 0xFF;
    s_gdt_table.tss_desc.Low.Access     = 0x89;   // Present, TSS64 Available, DPL=0
    s_gdt_table.tss_desc.Low.Granularity = (tss_size >> 16) & 0x0F;
    s_gdt_table.tss_desc.Low.BaseHigh   = (tss_base >> 24) & 0xFF;
    s_gdt_table.tss_desc.BaseUpper      = (tss_base >> 32) & 0xFFFFFFFF;
    s_gdt_table.tss_desc.Reserved       = 0;

    // ---- TSS: ring-0 stack ----
    s_tss.Rsp0      = reinterpret_cast<u64>(s_kernel_stack + sizeof(s_kernel_stack));
    s_tss.IoMapBase = static_cast<u16>(sizeof(Tss64));

    // ---- Build GDTR ----
    s_gdt_ptr.Limit = static_cast<u16>(sizeof(GdtTable) - 1);  // 56 - 1 = 55
    s_gdt_ptr.Base  = reinterpret_cast<u64>(&s_gdt_table);     // base = start of packed struct

    // ---- Load GDT and far-return to reload CS ----
    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq $0x08\n\t"                    // new CS
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"               // kernel data selector
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :: "m"(s_gdt_ptr) : "rax", "memory"
    );

    // ---- Load TSS ----
    __asm__ volatile("ltr %0" :: "rm"((u16)0x28));
}

} // namespace HAL
