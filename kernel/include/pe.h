#pragma once
// pe.h - MicroNT PE/COFF loader interface

#include "ntdef.h"
#include "ntstatus.h"

// ============================================================
// PE format constants
// ============================================================
constexpr u16 DOS_MAGIC       = 0x5A4D;  // 'MZ'
constexpr u32 PE_SIGNATURE    = 0x00004550; // 'PE\0\0'
constexpr u16 MACHINE_AMD64   = 0x8664;
constexpr u16 OPT_MAGIC_PE32P = 0x020B;  // PE32+

// ============================================================
// DOS header
// ============================================================
struct ImageDosHeader {
    u16 e_magic;
    u16 e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc;
    u16 e_ss, e_sp, e_csum, e_ip, e_cs, e_lfarlc, e_ovno;
    u16 e_res[4], e_oemid, e_oeminfo, e_res2[10];
    i32 e_lfanew;
};

// ============================================================
// COFF file header
// ============================================================
struct ImageFileHeader {
    u16 Machine;
    u16 NumberOfSections;
    u32 TimeDateStamp;
    u32 PointerToSymbolTable;
    u32 NumberOfSymbols;
    u16 SizeOfOptionalHeader;
    u16 Characteristics;
};

// ============================================================
// PE32+ optional header
// ============================================================
struct ImageOptionalHeader64 {
    u16 Magic;
    u8  MajorLinkerVersion, MinorLinkerVersion;
    u32 SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData;
    u32 AddressOfEntryPoint;
    u32 BaseOfCode;
    u64 ImageBase;
    u32 SectionAlignment, FileAlignment;
    u16 MajorOSVersion, MinorOSVersion, MajorImageVersion, MinorImageVersion;
    u16 MajorSubsystemVersion, MinorSubsystemVersion;
    u32 Win32VersionValue, SizeOfImage, SizeOfHeaders, CheckSum;
    u16 Subsystem, DllCharacteristics;
    u64 SizeOfStackReserve, SizeOfStackCommit;
    u64 SizeOfHeapReserve, SizeOfHeapCommit;
    u32 LoaderFlags, NumberOfRvaAndSizes;
    struct { u32 VirtualAddress, Size; } DataDirectory[16];
};

// ============================================================
// NT headers (PE32+)
// ============================================================
struct ImageNtHeaders64 {
    u32                    Signature;
    ImageFileHeader        FileHeader;
    ImageOptionalHeader64  OptionalHeader;
};

// ============================================================
// Section header
// ============================================================
struct ImageSectionHeader {
    char Name[8];
    u32  VirtualSize;
    u32  VirtualAddress;
    u32  SizeOfRawData;
    u32  PointerToRawData;
    u32  PointerToRelocations;
    u32  PointerToLinenumbers;
    u16  NumberOfRelocations;
    u16  NumberOfLinenumbers;
    u32  Characteristics;
};

constexpr u32 IMAGE_SCN_CNT_CODE               = 0x00000020;
constexpr u32 IMAGE_SCN_CNT_INITIALIZED_DATA   = 0x00000040;
constexpr u32 IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080;
constexpr u32 IMAGE_SCN_MEM_EXECUTE            = 0x20000000;
constexpr u32 IMAGE_SCN_MEM_READ               = 0x40000000;
constexpr u32 IMAGE_SCN_MEM_WRITE              = 0x80000000;

// ============================================================
// Import tables
// ============================================================
struct ImageImportDescriptor {
    u32 OriginalFirstThunk;
    u32 TimeDateStamp;
    u32 ForwarderChain;
    u32 Name;
    u32 FirstThunk;
};

struct ImageThunkData64 {
    u64 AddressOfData;
};

struct ImageImportByName {
    u16  Hint;
    char Name[1];
};

// ============================================================
// Base relocation
// ============================================================
struct ImageBaseRelocation {
    u32 VirtualAddress;
    u32 SizeOfBlock;
};

constexpr u16 IMAGE_REL_BASED_ABSOLUTE = 0;
constexpr u16 IMAGE_REL_BASED_DIR64    = 10;

// Data directory indices
constexpr u32 IMAGE_DIRECTORY_ENTRY_EXPORT       = 0;
constexpr u32 IMAGE_DIRECTORY_ENTRY_IMPORT       = 1;
constexpr u32 IMAGE_DIRECTORY_ENTRY_BASERELOC    = 5;
constexpr u32 IMAGE_DIRECTORY_ENTRY_TLS          = 9;

// ============================================================
// PE Loader
// ============================================================
namespace LDR {

void Init();

// Load a PE image from a buffer into the current (kernel) address space.
// Returns entry point address or 0 on failure.
// M7: will use process address space.
NTSTATUS LoadImage(const u8* file_data, usize file_size,
                   u64* out_entry_point, u64* out_image_base);

// Resolve a single import DLL (stub for M1)
NTSTATUS ResolveDll(const char* name, u64* out_base);

} // namespace LDR
