#pragma once
// bootinfo.h - Boot information passed from UEFI bootloader to kernel.

constexpr unsigned long long BOOTINFO_MAGIC = 0x544E424D01ULL;

enum BootMemoryType : unsigned int {
    BOOT_MEM_RESERVED = 0,
    BOOT_MEM_AVAILABLE = 1,
    BOOT_MEM_ACPI     = 2,
    BOOT_MEM_NVS      = 3,
    BOOT_MEM_MMIO     = 4,
    BOOT_MEM_LOADER   = 5,
};
struct BootMemoryEntry {
    unsigned long long base, length;
    BootMemoryType     type;
    unsigned int       _pad;
};

// Files loaded from /boot/*.exe by the bootloader
constexpr unsigned int BOOT_FILES_MAX = 8;
struct BootFile {
    char               name[64];    // null-terminated filename
    unsigned long long phys_base;   // physical address of raw file data
    unsigned long long size;        // byte count
};

constexpr unsigned int BOOT_MEMORY_MAX = 256;
struct MicroNTBootInfo {
    unsigned long long  magic;
    unsigned long long  kernel_phys_base;
    unsigned long long  kernel_size;
    unsigned long long  rsdp_phys;
    unsigned long long  initrd_phys;
    unsigned long long  initrd_size;
    unsigned int        memory_entry_count;
    unsigned int        boot_file_count;
    BootFile            boot_files[BOOT_FILES_MAX];
    BootMemoryEntry     memory_map[BOOT_MEMORY_MAX];
};
