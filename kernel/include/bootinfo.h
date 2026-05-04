#pragma once
// bootinfo.h - Boot information passed from the UEFI bootloader to the kernel.
// Uses only primitive types so it can be included from both the freestanding
// kernel build and the UEFI bootloader (Windows/PE target).

// ============================================================
// Magic
// ============================================================
// 'MNBT' + version byte
constexpr unsigned long long BOOTINFO_MAGIC = 0x544E424D01ULL;

// ============================================================
// Memory map entry
// Simplified from EFI_MEMORY_DESCRIPTOR; only what the kernel PMM needs.
// ============================================================
enum BootMemoryType : unsigned int {
    BOOT_MEM_RESERVED   = 0,
    BOOT_MEM_AVAILABLE  = 1,   // conventional + reclaimed boot-services RAM
    BOOT_MEM_ACPI       = 2,   // reclaimable after OS parses ACPI tables
    BOOT_MEM_NVS        = 3,   // ACPI NVS — do NOT touch
    BOOT_MEM_MMIO       = 4,   // memory-mapped I/O
    BOOT_MEM_LOADER     = 5,   // bootloader / kernel image (NOT free)
};

struct BootMemoryEntry {
    unsigned long long  base;   // physical byte address (page-aligned)
    unsigned long long  length; // byte length (page-aligned)
    BootMemoryType      type;
    unsigned int        _pad;
};

// ============================================================
// MicroNTBootInfo — filled by the UEFI bootloader, read by kernel_main
// ============================================================
constexpr unsigned int BOOT_MEMORY_MAX = 256;

struct MicroNTBootInfo {
    unsigned long long  magic;              // must equal BOOTINFO_MAGIC
    unsigned long long  kernel_phys_base;   // where the ELF was loaded
    unsigned long long  kernel_size;        // total loaded size (bytes)
    unsigned long long  rsdp_phys;          // ACPI RSDP physical addr (0 if none)
    unsigned int        memory_entry_count; // valid entries in memory_map[]
    unsigned int        _pad;
    BootMemoryEntry     memory_map[BOOT_MEMORY_MAX];
};
