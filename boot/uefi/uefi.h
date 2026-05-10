#pragma once
// uefi.h - Minimal UEFI type definitions for MicroNT bootloader.
// Covers exactly the protocols and services we need.
// No EDK2 or gnu-efi dependency.

// ============================================================
// Primitive types
// ============================================================
using BOOLEAN  = unsigned char;
using INTN     = long long;
using UINTN    = unsigned long long;
using INT8     = signed char;
using UINT8    = unsigned char;
using INT16    = short;
using UINT16   = unsigned short;
using INT32    = int;
using UINT32   = unsigned int;
using INT64    = long long;
using UINT64   = unsigned long long;
using CHAR8    = char;
using CHAR16   = wchar_t;           // UTF-16 LE; wchar_t is 2 bytes on Windows/UEFI target

using EFI_STATUS           = UINTN;
using EFI_HANDLE           = void*;
using EFI_EVENT            = void*;
using EFI_PHYSICAL_ADDRESS = UINT64;
using EFI_VIRTUAL_ADDRESS  = UINT64;

// ============================================================
// Status codes
// ============================================================
#define EFI_SUCCESS             ((EFI_STATUS)0ULL)
#define EFI_ERR(x)              ((EFI_STATUS)((x) | (1ULL << 63)))
#define EFI_IS_ERROR(s)         (((s) & (1ULL << 63)) != 0)
#define EFI_LOAD_ERROR          EFI_ERR(1)
#define EFI_INVALID_PARAMETER   EFI_ERR(2)
#define EFI_UNSUPPORTED         EFI_ERR(3)
#define EFI_BUFFER_TOO_SMALL    EFI_ERR(5)
#define EFI_OUT_OF_RESOURCES    EFI_ERR(9)
#define EFI_NOT_FOUND           EFI_ERR(14)

// ============================================================
// GUID
// ============================================================
struct EFI_GUID {
    UINT32 Data1;
    UINT16 Data2, Data3;
    UINT8  Data4[8];
};

inline bool GuidEqual(const EFI_GUID& a, const EFI_GUID& b) {
    if (a.Data1 != b.Data1 || a.Data2 != b.Data2 || a.Data3 != b.Data3)
        return false;
    for (int i = 0; i < 8; ++i)
        if (a.Data4[i] != b.Data4[i]) return false;
    return true;
}

// ============================================================
// Memory
// ============================================================
enum EFI_MEMORY_TYPE : UINT32 {
    EfiReservedMemoryType      = 0,
    EfiLoaderCode              = 1,
    EfiLoaderData              = 2,
    EfiBootServicesCode        = 3,
    EfiBootServicesData        = 4,
    EfiRuntimeServicesCode     = 5,
    EfiRuntimeServicesData     = 6,
    EfiConventionalMemory      = 7,
    EfiUnusableMemory          = 8,
    EfiACPIReclaimMemory       = 9,
    EfiACPIMemoryNVS           = 10,
    EfiMemoryMappedIO          = 11,
    EfiMemoryMappedIOPortSpace = 12,
    EfiPalCode                 = 13,
    EfiPersistentMemory        = 14,
};

enum EFI_ALLOCATE_TYPE {
    AllocateAnyPages   = 0,
    AllocateMaxAddress = 1,
    AllocateAddress    = 2,
};

struct EFI_MEMORY_DESCRIPTOR {
    UINT32               Type;
    UINT32               _pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    UINT64               NumberOfPages;
    UINT64               Attribute;
};

// ============================================================
// Table header
// ============================================================
struct EFI_TABLE_HEADER {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
};

// ============================================================
// Simple Text Output
// ============================================================
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (*Reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, BOOLEAN);
    EFI_STATUS (*OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, CHAR16*);
    void* QueryMode;
    void* SetMode;
    void* SetAttribute;
    EFI_STATUS (*ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*);
    void* SetCursorPosition;
    void* EnableCursor;
    void* Mode;
};

// ============================================================
// Boot Services (partial layout, ordered per UEFI spec §4.4)
// ============================================================
struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;

    // Task priority
    void* RaiseTPL;
    void* RestoreTPL;

    // Memory allocation
    EFI_STATUS (*AllocatePages)(EFI_ALLOCATE_TYPE, EFI_MEMORY_TYPE,
                                UINTN Pages, EFI_PHYSICAL_ADDRESS*);
    EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS, UINTN);
    EFI_STATUS (*GetMemoryMap)(UINTN* MapSize, EFI_MEMORY_DESCRIPTOR*,
                               UINTN* MapKey, UINTN* DescriptorSize,
                               UINT32* DescriptorVersion);
    EFI_STATUS (*AllocatePool)(EFI_MEMORY_TYPE, UINTN, void**);
    EFI_STATUS (*FreePool)(void*);

    // Events
    void* CreateEvent;
    void* SetTimer;
    void* WaitForEvent;
    void* SignalEvent;
    void* CloseEvent;
    void* CheckEvent;

    // Protocol handler services
    void* InstallProtocolInterface;
    void* ReinstallProtocolInterface;
    void* UninstallProtocolInterface;
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE, EFI_GUID*, void**);
    void* _Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;

    // Image services
    void* LoadImage;
    void* StartImage;
    void* Exit;
    void* UnloadImage;
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE, UINTN);

    // Misc
    void* GetNextMonotonicCount;
    void* Stall;
    EFI_STATUS (*SetWatchdogTimer)(UINTN Timeout, UINT64 WatchdogCode,
                                   UINTN DataSize, CHAR16* WatchdogData);

    // Driver support
    void* ConnectController;
    void* DisconnectController;

    // Open/close protocol
    void* OpenProtocol;
    void* CloseProtocol;
    void* OpenProtocolInformation;

    // Library services
    void* ProtocolsPerHandle;
    void* LocateHandleBuffer;
    EFI_STATUS (*LocateProtocol)(EFI_GUID*, void* Registration, void** Interface);
    void* InstallMultipleProtocolInterfaces;
    void* UninstallMultipleProtocolInterfaces;

    // CRC
    void* CalculateCrc32;

    // Misc
    void* CopyMem;
    void* SetMem;
    void* CreateEventEx;
};

// ============================================================
// Runtime Services (stub — not used in M1)
// ============================================================
struct EFI_RUNTIME_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void* Padding[14];
};

// ============================================================
// Configuration table entry
// ============================================================
struct EFI_CONFIGURATION_TABLE {
    EFI_GUID VendorGuid;
    void*    VendorTable;
};

// ============================================================
// System Table
// ============================================================
struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER              Hdr;
    CHAR16*                       FirmwareVendor;
    UINT32                        FirmwareRevision;
    EFI_HANDLE                    ConsoleInHandle;
    void*                         ConIn;
    EFI_HANDLE                    ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE                    StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;
    EFI_RUNTIME_SERVICES*         RuntimeServices;
    EFI_BOOT_SERVICES*            BootServices;
    UINTN                         NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE*      ConfigurationTable;
};

// ============================================================
// Loaded Image Protocol
// ============================================================
static const EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5B1B31A1, 0x9562, 0x11d2,
    {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
};

struct EFI_LOADED_IMAGE_PROTOCOL {
    UINT32            Revision;
    EFI_HANDLE        ParentHandle;
    EFI_SYSTEM_TABLE* SystemTable;
    EFI_HANDLE        DeviceHandle;
    void*             FilePath;
    void*             Reserved;
    UINT32            LoadOptionsSize;
    void*             LoadOptions;
    void*             ImageBase;
    UINT64            ImageSize;
    EFI_MEMORY_TYPE   ImageCodeType;
    EFI_MEMORY_TYPE   ImageDataType;
    void*             Unload;
};

// ============================================================
// Simple File System Protocol
// ============================================================
static const EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {
    0x964e5b22, 0x6459, 0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (*Open)(EFI_FILE_PROTOCOL* Self, EFI_FILE_PROTOCOL** NewHandle,
                       CHAR16* FileName, UINT64 OpenMode, UINT64 Attributes);
    EFI_STATUS (*Close)(EFI_FILE_PROTOCOL* Self);
    EFI_STATUS (*Delete)(EFI_FILE_PROTOCOL* Self);
    EFI_STATUS (*Read)(EFI_FILE_PROTOCOL* Self, UINTN* BufferSize, void* Buffer);
    EFI_STATUS (*Write)(EFI_FILE_PROTOCOL* Self, UINTN* BufferSize, void* Buffer);
    EFI_STATUS (*GetPosition)(EFI_FILE_PROTOCOL* Self, UINT64* Position);
    EFI_STATUS (*SetPosition)(EFI_FILE_PROTOCOL* Self, UINT64 Position);
    EFI_STATUS (*GetInfo)(EFI_FILE_PROTOCOL* Self, EFI_GUID* InformationType,
                          UINTN* BufferSize, void* Buffer);
    EFI_STATUS (*SetInfo)(EFI_FILE_PROTOCOL* Self, EFI_GUID* InformationType,
                          UINTN BufferSize, void* Buffer);
    EFI_STATUS (*Flush)(EFI_FILE_PROTOCOL* Self);
};

#define EFI_FILE_MODE_READ  0x0000000000000001ULL
#define EFI_FILE_READ_ONLY  0x0000000000000001ULL

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (*OpenVolume)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* Self,
                             EFI_FILE_PROTOCOL** Root);
};

// EFI_FILE_INFO GUID
static const EFI_GUID EFI_FILE_INFO_GUID = {
    0x09576e92, 0x6d3f, 0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

struct EFI_FILE_INFO {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    UINT64 CreateTime[2];
    UINT64 LastAccessTime[2];
    UINT64 ModificationTime[2];
    UINT64 Attribute;
    CHAR16 FileName[256];
};

// ============================================================
// ACPI Table GUIDs (to find RSDP)
// ============================================================
static const EFI_GUID EFI_ACPI_TABLE_GUID = {
    0x8868e871, 0xe4f1, 0x11d3,
    {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}
};

static const EFI_GUID EFI_ACPI_20_TABLE_GUID = {
    0x8868e871, 0xe4f1, 0x11d3,
    {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}
};

// ACPI 2.0 table GUID (correct one)
static const EFI_GUID ACPI_20_TABLE_GUID = {
    0x8868E871, 0xE4F1, 0x11D3,
    {0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81}
};

// ---------------------------------------------------------------------------
// Graphics Output Protocol (GOP) -- needed for framebuffer query
// ---------------------------------------------------------------------------
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042A9DE, 0x23DC, 0x4A38, \
      { 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A } }

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask                          = 2,
    PixelBltOnly                          = 3,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32                    Version;
    UINT32                    HorizontalResolution;
    UINT32                    VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    UINT32                    PixelInformation[4];
    UINT32                    PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                               MaxMode;
    UINT32                               Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN                                SizeOfInfo;
    EFI_PHYSICAL_ADDRESS                 FrameBufferBase;
    UINTN                                FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    void* QueryMode;
    void* SetMode;
    void* Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;
