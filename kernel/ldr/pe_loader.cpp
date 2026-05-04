// pe_loader.cpp - MicroNT PE loader (M1: validation + header parsing only)

#include "../include/pe.h"
#include "../include/debug.h"
#include "../include/memory.h"

namespace LDR {

void Init() {
    // TODO(M7): Register built-in DLLs, prepare import resolver
    KDBG_INFO("LDR: PE loader initialized (M1 stub)");
}

static bool ValidateHeaders(const u8* data, usize size,
                             const ImageNtHeaders64** out_nt) {
    if (size < sizeof(ImageDosHeader)) {
        KDBG_ERROR("LDR: file too small for DOS header");
        return false;
    }
    const auto* dos = reinterpret_cast<const ImageDosHeader*>(data);
    if (dos->e_magic != DOS_MAGIC) {
        KDBG_ERROR("LDR: bad DOS magic 0x%x", dos->e_magic);
        return false;
    }
    if (static_cast<usize>(dos->e_lfanew) + sizeof(ImageNtHeaders64) > size) {
        KDBG_ERROR("LDR: e_lfanew out of bounds");
        return false;
    }
    const auto* nt = reinterpret_cast<const ImageNtHeaders64*>(
        data + dos->e_lfanew);
    if (nt->Signature != PE_SIGNATURE) {
        KDBG_ERROR("LDR: bad PE signature");
        return false;
    }
    if (nt->FileHeader.Machine != MACHINE_AMD64) {
        KDBG_ERROR("LDR: not AMD64 (machine=0x%x)", nt->FileHeader.Machine);
        return false;
    }
    if (nt->OptionalHeader.Magic != OPT_MAGIC_PE32P) {
        KDBG_ERROR("LDR: not PE32+ (magic=0x%x)", nt->OptionalHeader.Magic);
        return false;
    }
    *out_nt = nt;
    return true;
}

NTSTATUS LoadImage(const u8* file_data, usize file_size,
                   u64* out_entry_point, u64* out_image_base) {
    const ImageNtHeaders64* nt = nullptr;
    if (!ValidateHeaders(file_data, file_size, &nt))
        return STATUS_INVALID_IMAGE_FORMAT;

    usize image_size = nt->OptionalHeader.SizeOfImage;
    u64   preferred  = nt->OptionalHeader.ImageBase;

    KDBG_INFO("LDR: Loading PE image size=%llu preferred_base=0x%llx",
        (u64)image_size, preferred);

    // Allocate pages for the image
    usize pages = AlignUp<usize>(image_size, (usize)PAGE_SIZE) / PAGE_SIZE;
    u64 load_base = PMM::AllocPages(pages);
    if (!load_base) {
        KDBG_ERROR("LDR: out of memory for image (%llu pages)", (u64)pages);
        return STATUS_NO_MEMORY;
    }

    // Zero the image region
    for (usize i = 0; i < image_size; ++i)
        reinterpret_cast<u8*>(load_base)[i] = 0;

    // Copy headers
    const auto& opt = nt->OptionalHeader;
    for (u32 i = 0; i < opt.SizeOfHeaders && i < file_size; ++i)
        reinterpret_cast<u8*>(load_base)[i] = file_data[i];

    // Copy sections
    const auto* sections = reinterpret_cast<const ImageSectionHeader*>(
        reinterpret_cast<const u8*>(&nt->OptionalHeader)
        + nt->FileHeader.SizeOfOptionalHeader);

    for (u16 i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sections[i];
        if (s.SizeOfRawData == 0) continue;

        u64 dst = load_base + s.VirtualAddress;
        const u8* src = file_data + s.PointerToRawData;
        u32 copy_size = s.SizeOfRawData < s.VirtualSize
                        ? s.SizeOfRawData : s.VirtualSize;

        char name[9] = {};
        for (int j = 0; j < 8; ++j) name[j] = s.Name[j];
        KDBG_TRACE("LDR:   section %s VA=0x%x size=%u",
            name, s.VirtualAddress, copy_size);

        for (u32 j = 0; j < copy_size; ++j)
            reinterpret_cast<u8*>(dst)[j] = src[j];
    }

    // Apply base relocations if load address differs from preferred
    i64 delta = static_cast<i64>(load_base) - static_cast<i64>(preferred);
    if (delta != 0) {
        const auto& reloc_dir = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (reloc_dir.VirtualAddress && reloc_dir.Size) {
            auto* block = reinterpret_cast<ImageBaseRelocation*>(
                load_base + reloc_dir.VirtualAddress);
            auto* end   = reinterpret_cast<ImageBaseRelocation*>(
                load_base + reloc_dir.VirtualAddress + reloc_dir.Size);

            while (block < end && block->SizeOfBlock) {
                u32 count = (block->SizeOfBlock - sizeof(ImageBaseRelocation)) / 2;
                auto* entries = reinterpret_cast<u16*>(block + 1);
                for (u32 k = 0; k < count; ++k) {
                    u16 type   = entries[k] >> 12;
                    u16 offset = entries[k] & 0xFFF;
                    if (type == IMAGE_REL_BASED_DIR64) {
                        u64* target = reinterpret_cast<u64*>(
                            load_base + block->VirtualAddress + offset);
                        *target += delta;
                    }
                }
                block = reinterpret_cast<ImageBaseRelocation*>(
                    reinterpret_cast<u8*>(block) + block->SizeOfBlock);
            }
        }
    }

    // TODO(M7): Resolve imports from initrd DLLs

    *out_entry_point = load_base + opt.AddressOfEntryPoint;
    *out_image_base  = load_base;

    KDBG_INFO("LDR: Image loaded at 0x%llx entry=0x%llx",
        load_base, *out_entry_point);
    return STATUS_SUCCESS;
}

NTSTATUS ResolveDll(const char* name, u64* out_base) {
    // TODO(M7): Look up DLL in initrd archive and load it
    KDBG_WARN("LDR: ResolveDll('%s') stub — returning not-found", name);
    UNUSED(out_base);
    return STATUS_DLL_NOT_FOUND;
}

} // namespace LDR
