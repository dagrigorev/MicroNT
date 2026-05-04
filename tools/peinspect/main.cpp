// peinspect/main.cpp - MicroNT PE inspection utility (host Windows tool)
// Dumps headers and sections of a PE32+ executable.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// ---- minimal PE types (duplicated from kernel/include/pe.h for host build) ---
using u8  = uint8_t;  using u16 = uint16_t;
using u32 = uint32_t; using u64 = uint64_t;
using i32 = int32_t;

constexpr u16 DOS_MAGIC       = 0x5A4D;
constexpr u32 PE_SIGNATURE    = 0x00004550;
constexpr u16 MACHINE_AMD64   = 0x8664;
constexpr u16 OPT_MAGIC_PE32P = 0x020B;

#pragma pack(push,1)
struct ImageDosHeader   { u16 e_magic; u16 _pad[28]; i32 e_lfanew; };
struct ImageFileHeader  { u16 Machine, NumberOfSections; u32 TimeDateStamp,
                          PointerToSymbolTable, NumberOfSymbols;
                          u16 SizeOfOptionalHeader, Characteristics; };
struct ImageOptionalHeader64 {
    u16 Magic; u8 MajorLinker, MinorLinker;
    u32 SizeOfCode, SizeOfInitData, SizeOfUninitData;
    u32 AddressOfEntryPoint, BaseOfCode;
    u64 ImageBase;
    u32 SectionAlignment, FileAlignment;
    u16 MajorOS, MinorOS, MajorImage, MinorImage, MajorSS, MinorSS;
    u32 Win32Version, SizeOfImage, SizeOfHeaders, CheckSum;
    u16 Subsystem, DllCharacteristics;
    u64 SizeOfStackReserve, SizeOfStackCommit, SizeOfHeapReserve, SizeOfHeapCommit;
    u32 LoaderFlags, NumberOfRvaAndSizes;
    struct { u32 VirtualAddress, Size; } DataDirectory[16];
};
struct ImageNtHeaders64 { u32 Signature; ImageFileHeader FileHeader;
                          ImageOptionalHeader64 OptionalHeader; };
struct ImageSectionHeader { char Name[8]; u32 VirtualSize, VirtualAddress,
    SizeOfRawData, PointerToRawData, PointerToRelocations, PointerToLinenumbers;
    u16 NumberOfRelocations, NumberOfLinenumbers; u32 Characteristics; };
#pragma pack(pop)

static const char* subsystem_name(u16 ss) {
    switch (ss) {
    case 1:  return "Native";
    case 2:  return "Win32 GUI";
    case 3:  return "Win32 CUI";
    case 5:  return "OS/2 CUI";
    case 7:  return "POSIX CUI";
    case 9:  return "Win9x Driver";
    case 10: return "WinCE GUI";
    case 14: return "EFI Application";
    default: return "Unknown";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: peinspect <file.exe>\n");
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    u8* data = (u8*)malloc(size);
    if (!data) { fprintf(stderr, "OOM\n"); return 1; }
    fread(data, 1, size, f);
    fclose(f);

    // Validate DOS header
    if (size < (long)sizeof(ImageDosHeader)) { fprintf(stderr, "Too small\n"); return 1; }
    auto* dos = (ImageDosHeader*)data;
    if (dos->e_magic != DOS_MAGIC) { fprintf(stderr, "Not a PE (bad MZ)\n"); return 1; }

    auto* nt = (ImageNtHeaders64*)(data + dos->e_lfanew);
    if (nt->Signature != PE_SIGNATURE) { fprintf(stderr, "Bad PE signature\n"); return 1; }

    auto& fh = nt->FileHeader;
    auto& oh = nt->OptionalHeader;

    printf("=== PE32+ Dump: %s ===\n\n", argv[1]);
    printf("Machine          : 0x%04X (%s)\n", fh.Machine,
        fh.Machine == MACHINE_AMD64 ? "AMD64" : "?");
    printf("Sections         : %u\n", fh.NumberOfSections);
    printf("TimeDateStamp    : 0x%08X\n", fh.TimeDateStamp);
    printf("Characteristics  : 0x%04X\n", fh.Characteristics);
    printf("\n");
    printf("Optional header:\n");
    printf("  Magic          : 0x%04X (%s)\n", oh.Magic,
        oh.Magic == OPT_MAGIC_PE32P ? "PE32+" : "PE32");
    printf("  ImageBase      : 0x%016llX\n", (unsigned long long)oh.ImageBase);
    printf("  EntryPoint RVA : 0x%08X\n", oh.AddressOfEntryPoint);
    printf("  SizeOfImage    : 0x%08X (%u KB)\n", oh.SizeOfImage, oh.SizeOfImage/1024);
    printf("  SizeOfHeaders  : 0x%08X\n", oh.SizeOfHeaders);
    printf("  Subsystem      : %u (%s)\n", oh.Subsystem, subsystem_name(oh.Subsystem));
    printf("  SectionAlign   : 0x%X\n", oh.SectionAlignment);
    printf("  FileAlign      : 0x%X\n", oh.FileAlignment);
    printf("\n");

    // Data directories
    static const char* dd_names[] = {
        "Export","Import","Resource","Exception","Security",
        "BaseReloc","Debug","Architecture","GlobalPtr","TLS",
        "LoadConfig","BoundImport","IAT","DelayImport","CLR","Reserved"
    };
    printf("Data directories:\n");
    for (int i = 0; i < 16; ++i) {
        if (oh.DataDirectory[i].VirtualAddress || oh.DataDirectory[i].Size) {
            printf("  [%2d] %-12s RVA=0x%08X  Size=0x%X\n",
                i, dd_names[i],
                oh.DataDirectory[i].VirtualAddress,
                oh.DataDirectory[i].Size);
        }
    }
    printf("\n");

    // Sections
    auto* sects = (ImageSectionHeader*)(
        (u8*)&nt->OptionalHeader + fh.SizeOfOptionalHeader);
    printf("Sections:\n");
    printf("  %-8s  %-10s %-10s %-10s %-10s  %s\n",
        "Name", "VirtAddr", "VirtSize", "RawOff", "RawSize", "Flags");
    for (int i = 0; i < fh.NumberOfSections; ++i) {
        char name[9] = {}; memcpy(name, sects[i].Name, 8);
        printf("  %-8s  0x%08X 0x%08X 0x%08X 0x%08X  0x%08X\n",
            name,
            sects[i].VirtualAddress,
            sects[i].VirtualSize,
            sects[i].PointerToRawData,
            sects[i].SizeOfRawData,
            sects[i].Characteristics);
    }

    free(data);
    return 0;
}
