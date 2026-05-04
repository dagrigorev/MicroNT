#pragma once
// boot_info.h - MicroNT boot information passed from UEFI bootloader to kernel.
// Plain C so it can be included in both the C bootloader and the C++ kernel.
// No <stdint.h> — uses compiler builtin type macros for -nostdinc compatibility.

#define BOOT_INFO_MAGIC  0x464E49544F4F424EULL  // "NBOOTINF" little-endian

// Memory map entry types (mirrors UEFI + E820 conventions)
#define MMAP_USABLE   1u
#define MMAP_RESERVED 2u
#define MMAP_ACPI     3u
#define MMAP_ACPI_NVS 4u
#define MMAP_BAD      5u

#define MAX_MMAP_ENTRIES 256

typedef __UINT64_TYPE__ boot_u64;
typedef __UINT32_TYPE__ boot_u32;

typedef struct {
    boot_u64 base;
    boot_u64 length;
    boot_u32 type;    // MMAP_* constants above
    boot_u32 _pad;
} MemMapEntry;

typedef struct {
    boot_u64 magic;              // BOOT_INFO_MAGIC
    boot_u64 kernel_phys_start;  // physical start of loaded kernel
    boot_u64 kernel_phys_end;    // physical end of loaded kernel
    boot_u32 mem_map_count;      // valid entries in mem_map[]
    boot_u32 _pad;

    // GOP framebuffer (0 if no GOP found)
    boot_u64 fb_addr;
    boot_u32 fb_width;
    boot_u32 fb_height;
    boot_u32 fb_pitch;     // bytes per scan line
    boot_u32 fb_format;    // 0=BGRX  1=RGBX

    // Inline memory map (avoids a separate allocation)
    MemMapEntry mem_map[MAX_MMAP_ENTRIES];
} BootInfo;
