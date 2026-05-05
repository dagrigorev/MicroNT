// pe_loader.cpp - MicroNT M7 PE32+ loader
//
// Parses a PE32+ image in memory and maps its sections into a user
// process address space using VMM::MapPageInto.
//
// Supported: AMD64 PE32+, native subsystem, one .text section.
// Not supported (M7): imports, exports, TLS, debug info, ASLR.

#include "../include/pe.h"
#include "../include/memory.h"
#include "../include/debug.h"
#include "../include/ntstatus.h"

namespace {

// PE constants
constexpr u16 IMAGE_DOS_SIGNATURE     = 0x5A4D;  // MZ
constexpr u32 IMAGE_NT_SIGNATURE      = 0x00004550; // PE\0\0
constexpr u16 IMAGE_FILE_MACHINE_AMD64 = 0x8664;
constexpr u16 IMAGE_NT_OPTIONAL_HDR64_MAGIC = 0x020B;

// Section flags -> page flags
static u64 SectionFlags(u32 chars) {
    u64 flags = VMM::PTE_PRESENT | VMM::PTE_USER;
    if (chars & 0x80000000u) flags |= VMM::PTE_WRITABLE;  // IMAGE_SCN_MEM_WRITE
    // NX: we leave NX off for simplicity (all sections executable)
    return flags;
}

} // namespace

namespace LDR {

void Init() {
    KDBG_INFO("LDR: PE loader initialized (M7)");
}

// ============================================================
// LoadPe
//
// pe_data     - raw PE image bytes in kernel memory
// pe_size     - byte count
// pml4_phys   - target process PML4 (for MapPageInto)
// load_base   - virtual address where the image will be placed
//               (must equal or override ImageBase; we always use this)
// entry_out   - receives the absolute entry-point VA on success
//
// Returns STATUS_SUCCESS or an error NTSTATUS.
// ============================================================
NTSTATUS LoadPe(const void* pe_data, usize pe_size,
                u64 pml4_phys, u64 load_base, u64* entry_out) {

    if (!pe_data || pe_size < 64 || !entry_out) return STATUS_INVALID_PARAMETER;

    auto* base8 = static_cast<const u8*>(pe_data);

    // ---- DOS header ----------------------------------------
    if (base8[0] != 'M' || base8[1] != 'Z') {
        KDBG_ERROR("LDR: missing MZ signature");
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    u32 e_lfanew = *reinterpret_cast<const u32*>(base8 + 60);
    if (e_lfanew + 4 + 20 + 4 > pe_size) {
        KDBG_ERROR("LDR: e_lfanew out of bounds (%u)", e_lfanew);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    // ---- NT signature + COFF header ------------------------
    const u32* pe_sig = reinterpret_cast<const u32*>(base8 + e_lfanew);
    if (*pe_sig != IMAGE_NT_SIGNATURE) {
        KDBG_ERROR("LDR: bad PE signature");
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    const u8* coff = base8 + e_lfanew + 4;
    u16 machine           = *reinterpret_cast<const u16*>(coff + 0);
    u16 num_sections      = *reinterpret_cast<const u16*>(coff + 2);
    u16 opt_hdr_size      = *reinterpret_cast<const u16*>(coff + 16);

    if (machine != IMAGE_FILE_MACHINE_AMD64) {
        KDBG_ERROR("LDR: not AMD64 (machine=0x%x)", machine);
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    if (num_sections == 0 || num_sections > 96) {
        KDBG_ERROR("LDR: invalid section count %u", num_sections);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    // ---- Optional header (PE32+) ---------------------------
    const u8* opt = coff + 20;
    if (opt_hdr_size < 0x70) {
        KDBG_ERROR("LDR: optional header too small (%u)", opt_hdr_size);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    u16 opt_magic = *reinterpret_cast<const u16*>(opt + 0);
    if (opt_magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        KDBG_ERROR("LDR: not PE32+ (magic=0x%x)", opt_magic);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    u32 entry_rva        = *reinterpret_cast<const u32*>(opt + 16);
    u64 preferred_base   = *reinterpret_cast<const u64*>(opt + 24);
    u32 sect_align       = *reinterpret_cast<const u32*>(opt + 32);
    u32 size_of_image    = *reinterpret_cast<const u32*>(opt + 56);
    u32 size_of_headers  = *reinterpret_cast<const u32*>(opt + 60);

    KDBG_INFO("LDR: PE32+ preferred_base=0x%llx entry_rva=0x%x sections=%u",
              preferred_base, entry_rva, num_sections);
    KDBG_INFO("LDR: loading at 0x%llx size=0x%x sect_align=0x%x",
              load_base, size_of_image, sect_align);

    // If load_base differs from preferred_base, we'd need relocations.
    // For M7, our hello.exe is built at load_base, so skip reloc.
    if (load_base != preferred_base) {
        KDBG_WARN("LDR: load_base differs from preferred_base - relocs not applied");
    }

    // ---- Section headers -----------------------------------
    const u8* sect_base = opt + opt_hdr_size;

    for (u16 s = 0; s < num_sections; ++s) {
        const u8* sh = sect_base + s * 40;  // sizeof(IMAGE_SECTION_HEADER) = 40

        u32 virtual_size   = *reinterpret_cast<const u32*>(sh + 8);
        u32 virtual_rva    = *reinterpret_cast<const u32*>(sh + 12);
        u32 raw_size       = *reinterpret_cast<const u32*>(sh + 16);
        u32 raw_offset     = *reinterpret_cast<const u32*>(sh + 20);
        u32 characteristics = *reinterpret_cast<const u32*>(sh + 36);

        char name[9] = {};
        for (int i = 0; i < 8; ++i) name[i] = sh[i];

        if (virtual_size == 0) continue;
        if (virtual_rva == 0) continue;

        u64 sect_va  = load_base + virtual_rva;
        u64 page_flags = SectionFlags(characteristics);

        KDBG_INFO("LDR: section '%s' rva=0x%x va=0x%llx vsz=0x%x rsz=0x%x",
                  name, virtual_rva, sect_va, virtual_size, raw_size);

        // Map each 4 KB page of this section
        usize pages = ((usize)virtual_size + PAGE_SIZE - 1) / PAGE_SIZE;
        for (usize pg = 0; pg < pages; ++pg) {
            u64 page_va  = sect_va + pg * PAGE_SIZE;
            u64 page_phys = PMM::AllocPage();
            if (!page_phys) {
                KDBG_ERROR("LDR: out of physical pages for section '%s'", name);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            // Zero the page
            auto* page_ptr = reinterpret_cast<u8*>(page_phys);
            for (usize i = 0; i < PAGE_SIZE; ++i) page_ptr[i] = 0;

            // Copy raw data into page (if available)
            usize file_offset = raw_offset + pg * PAGE_SIZE;
            if (raw_offset > 0 && raw_size > 0 && file_offset < pe_size) {
                usize copy_size = PAGE_SIZE;
                if (file_offset + copy_size > pe_size)
                    copy_size = pe_size - file_offset;
                if (copy_size > raw_size)
                    copy_size = raw_size;
                // Don't copy past the raw section
                usize sect_raw_end = raw_offset + raw_size;
                if (file_offset >= sect_raw_end) copy_size = 0;
                if (copy_size > 0) {
                    const u8* src = base8 + file_offset;
                    for (usize i = 0; i < copy_size; ++i)
                        page_ptr[i] = src[i];
                }
            }

            if (!VMM::MapPageInto(pml4_phys, page_va, page_phys, page_flags)) {
                KDBG_ERROR("LDR: MapPageInto failed at 0x%llx", page_va);
                PMM::FreePage(page_phys);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
        }
    }

    *entry_out = load_base + entry_rva;
    KDBG_INFO("LDR: entry point -> 0x%llx", *entry_out);
    return STATUS_SUCCESS;
}

} // namespace LDR
