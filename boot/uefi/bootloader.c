// bootloader.c - MicroNT UEFI bootloader
//
// Compiled as a PE32+ EFI application targeting x86_64-unknown-windows.
// The kernel ELF is embedded as a blob (kernel_blob_start/end symbols
// from kernel_blob.asm).  No filesystem access is required at runtime.
//
// Boot flow:
//   1. Validate embedded ELF
//   2. Load ELF PT_LOAD segments into physical memory (AllocatePages)
//   3. Get GOP framebuffer info
//   4. Get UEFI memory map + ExitBootServices
//   5. Build BootInfo from UEFI memory map
//   6. Call kernel_entry(BootInfo*) using SysV ABI

#include "efi.h"
#include "../../kernel/include/boot_info.h"

// ---------------------------------------------------------------------------
// Embedded kernel blob (from kernel_blob.asm via NASM incbin)
// ---------------------------------------------------------------------------
extern const char kernel_blob_start[];
extern const char kernel_blob_end[];

// ---------------------------------------------------------------------------
// ELF64 types
// ---------------------------------------------------------------------------
typedef struct {
    UINT8  e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} Elf64_Phdr;

#define ELF_MAG0     0x7F
#define ELF_MAG1     'E'
#define ELF_MAG2     'L'
#define ELF_MAG3     'F'
#define ET_EXEC      2
#define EM_X86_64    62
#define PT_LOAD      1
#define ELFCLASS64   2
#define ELFDATA2LSB  1

// ---------------------------------------------------------------------------
// Minimal C runtime helpers (freestanding — no libc)
// ---------------------------------------------------------------------------
static void* bl_memset(void* dst, int val, UINTN n) {
    unsigned char* p = (unsigned char*)dst;
    while (n--) *p++ = (unsigned char)val;
    return dst;
}

static void* bl_memcpy(void* dst, const void* src, UINTN n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

// ---------------------------------------------------------------------------
// Early console helpers (UTF-16 literals work because the source is UTF-8
// and clang/lld-link produce UTF-16 CHAR16 string literals via L"...")
// ---------------------------------------------------------------------------
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* g_con;

static void con_print(const CHAR16* s) {
    if (g_con) g_con->OutputString(g_con, (CHAR16*)s);
}

static void con_print_hex(UINT64 v) {
    CHAR16 buf[19];
    buf[0] = L'0'; buf[1] = L'x';
    for (int i = 15; i >= 2; --i) {
        UINT64 nibble = v & 0xF;
        buf[i] = (CHAR16)(nibble < 10 ? L'0' + nibble : L'A' + nibble - 10);
        v >>= 4;
    }
    buf[18] = 0;
    con_print(buf);
}

static void con_println(const CHAR16* s) {
    con_print(s);
    con_print(L"\r\n");
}

// Halt the CPU — used on fatal errors before ExitBootServices
static void halt(void) {
    for (;;) __asm__ volatile("cli; hlt");
}

// ---------------------------------------------------------------------------
// efi_main — UEFI application entry point
// ---------------------------------------------------------------------------
EFI_STATUS efi_main(EFI_HANDLE img_handle, EFI_SYSTEM_TABLE* st) {
    g_con = st->ConOut;
    con_println(L"");
    con_println(L"MicroNT UEFI Bootloader v0.1");
    con_println(L"-------------------------------");

    // -----------------------------------------------------------------------
    // 1. Validate embedded kernel ELF
    // -----------------------------------------------------------------------
    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)kernel_blob_start;

    if (ehdr->e_ident[0] != ELF_MAG0 ||
        ehdr->e_ident[1] != ELF_MAG1 ||
        ehdr->e_ident[2] != ELF_MAG2 ||
        ehdr->e_ident[3] != ELF_MAG3) {
        con_println(L"ERROR: kernel blob has bad ELF magic");
        halt();
    }
    if (ehdr->e_ident[4] != ELFCLASS64 || ehdr->e_ident[5] != ELFDATA2LSB) {
        con_println(L"ERROR: kernel is not ELF64 little-endian");
        halt();
    }
    if (ehdr->e_machine != EM_X86_64) {
        con_println(L"ERROR: kernel is not x86_64");
        halt();
    }

    con_print(L"  Kernel entry : "); con_print_hex(ehdr->e_entry); con_print(L"\r\n");
    con_print(L"  Program hdrs : "); con_print_hex(ehdr->e_phnum);  con_print(L"\r\n");

    // -----------------------------------------------------------------------
    // 2. Load PT_LOAD segments into physical memory
    // -----------------------------------------------------------------------
    const Elf64_Phdr* phdrs =
        (const Elf64_Phdr*)(kernel_blob_start + ehdr->e_phoff);

    EFI_PHYSICAL_ADDRESS kernel_phys_start = (EFI_PHYSICAL_ADDRESS)-1LL;
    EFI_PHYSICAL_ADDRESS kernel_phys_end   = 0;

    for (UINT16 i = 0; i < ehdr->e_phnum; ++i) {
        const Elf64_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        UINTN pages = (ph->p_memsz + 0xFFF) >> 12;
        EFI_PHYSICAL_ADDRESS addr = ph->p_paddr;

        EFI_STATUS s = st->BootServices->AllocatePages(
            AllocateAddress, EfiLoaderData, pages, &addr);
        if (EFI_ERROR(s)) {
            con_print(L"ERROR: AllocatePages failed at ");
            con_print_hex(ph->p_paddr);
            con_print(L" status="); con_print_hex(s);
            con_println(L"");
            halt();
        }

        // Zero whole virtual size then copy file data
        bl_memset((void*)(UINTN)addr, 0, ph->p_memsz);
        bl_memcpy((void*)(UINTN)addr,
                  kernel_blob_start + ph->p_offset,
                  ph->p_filesz);

        if (addr < kernel_phys_start) kernel_phys_start = addr;
        EFI_PHYSICAL_ADDRESS seg_end = addr + ph->p_memsz;
        if (seg_end > kernel_phys_end) kernel_phys_end = seg_end;

        con_print(L"  Segment load : "); con_print_hex(addr);
        con_print(L" + "); con_print_hex(ph->p_memsz); con_print(L"\r\n");
    }

    con_print(L"  Kernel range : "); con_print_hex(kernel_phys_start);
    con_print(L" - "); con_print_hex(kernel_phys_end); con_print(L"\r\n");

    // -----------------------------------------------------------------------
    // 3. Allocate BootInfo (2 pages = 8 KB, covers struct + mmap[256])
    // -----------------------------------------------------------------------
    EFI_PHYSICAL_ADDRESS bi_addr = 0;
    if (EFI_ERROR(st->BootServices->AllocatePages(
            AllocateAnyPages, EfiLoaderData, 2, &bi_addr))) {
        con_println(L"ERROR: BootInfo allocation failed");
        halt();
    }
    BootInfo* boot_info = (BootInfo*)(UINTN)bi_addr;
    bl_memset(boot_info, 0, 2 * 4096);

    // -----------------------------------------------------------------------
    // 4. GOP framebuffer (best-effort — not fatal if missing)
    // -----------------------------------------------------------------------
    {
        EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
        EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
        if (!EFI_ERROR(st->BootServices->LocateProtocol(
                &gop_guid, NULL, (VOID**)&gop)) && gop) {
            boot_info->fb_addr   = gop->Mode->FrameBufferBase;
            boot_info->fb_width  = gop->Mode->Info->HorizontalResolution;
            boot_info->fb_height = gop->Mode->Info->VerticalResolution;
            boot_info->fb_pitch  = gop->Mode->Info->PixelsPerScanLine * 4;
            boot_info->fb_format =
                (gop->Mode->Info->PixelFormat ==
                 PixelRedGreenBlueReserved8BitPerColor) ? 1 : 0;
            con_print(L"  Framebuffer  : "); con_print_hex(boot_info->fb_addr);
            con_print(L"  "); con_print_hex(boot_info->fb_width);
            con_print(L"x"); con_print_hex(boot_info->fb_height); con_print(L"\r\n");
        } else {
            con_println(L"  Framebuffer  : none");
        }
    }

    // -----------------------------------------------------------------------
    // 5. Get UEFI memory map + ExitBootServices
    //    Pattern: probe size → allocate → get map → ExitBootServices
    //    One retry is allowed if the map key went stale (AllocatePool above
    //    may have changed the map).
    // -----------------------------------------------------------------------
    UINTN map_size = 0, map_key = 0, desc_size = 0;
    UINT32 desc_ver = 0;
    VOID* map_buf = NULL;

    // Probe required buffer size (returns EFI_BUFFER_TOO_SMALL, updates map_size)
    st->BootServices->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
    map_size += 32 * desc_size;   // extra room for new entries from AllocatePool

    if (EFI_ERROR(st->BootServices->AllocatePool(EfiLoaderData, map_size, &map_buf))) {
        con_println(L"ERROR: memory map allocation failed");
        halt();
    }

    // Get map (may return a slightly different map_size)
    if (EFI_ERROR(st->BootServices->GetMemoryMap(
            &map_size, (EFI_MEMORY_DESCRIPTOR*)map_buf,
            &map_key, &desc_size, &desc_ver))) {
        con_println(L"ERROR: GetMemoryMap failed");
        halt();
    }

    con_println(L"  Exiting boot services...");

    EFI_STATUS exit_s = st->BootServices->ExitBootServices(img_handle, map_key);
    if (EFI_ERROR(exit_s)) {
        // Map key stale — retry without any new allocations
        st->BootServices->GetMemoryMap(&map_size, (EFI_MEMORY_DESCRIPTOR*)map_buf,
                                        &map_key, &desc_size, &desc_ver);
        exit_s = st->BootServices->ExitBootServices(img_handle, map_key);
        if (EFI_ERROR(exit_s)) {
            // Can't print anymore; just halt
            halt();
        }
    }

    // -----------------------------------------------------------------------
    // 6. Build BootInfo memory map from UEFI descriptors
    //    (no boot services from this point forward)
    // -----------------------------------------------------------------------
    UINTN entry_count = map_size / desc_size;
    UINT32 our_count  = 0;

    for (UINTN i = 0; i < entry_count && our_count < MAX_MMAP_ENTRIES; ++i) {
        const EFI_MEMORY_DESCRIPTOR* d =
            (const EFI_MEMORY_DESCRIPTOR*)((const char*)map_buf + i * desc_size);

        UINT32 type;
        switch (d->Type) {
        case EfiConventionalMemory:
        case EfiBootServicesCode:
        case EfiBootServicesData:
            type = MMAP_USABLE;
            break;
        case EfiACPIReclaimMemory:
            type = MMAP_ACPI;
            break;
        case EfiACPIMemoryNVS:
            type = MMAP_ACPI_NVS;
            break;
        case EfiUnusableMemory:
            type = MMAP_BAD;
            break;
        default:
            type = MMAP_RESERVED;
            break;
        }

        boot_info->mem_map[our_count].base   = d->PhysicalStart;
        boot_info->mem_map[our_count].length = d->NumberOfPages << 12;
        boot_info->mem_map[our_count].type   = type;
        boot_info->mem_map[our_count]._pad   = 0;
        ++our_count;
    }

    // -----------------------------------------------------------------------
    // 7. Finalise BootInfo header
    // -----------------------------------------------------------------------
    boot_info->magic             = BOOT_INFO_MAGIC;
    boot_info->kernel_phys_start = kernel_phys_start;
    boot_info->kernel_phys_end   = kernel_phys_end;
    boot_info->mem_map_count     = our_count;

    // -----------------------------------------------------------------------
    // 8. Transfer control to kernel entry point using SysV ABI
    //    (bootloader is MS x64; kernel is SysV — use __attribute__((sysv_abi))
    //    so clang puts boot_info in RDI, not RCX)
    // -----------------------------------------------------------------------
    typedef void (*KernelFn)(BootInfo*) __attribute__((sysv_abi));
    KernelFn kernel = (KernelFn)(UINTN)ehdr->e_entry;
    kernel(boot_info);

    // Should never reach here
    halt();
    return EFI_SUCCESS;
}
