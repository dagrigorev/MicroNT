#pragma once
// efi.h - Minimal UEFI type definitions for the MicroNT bootloader.
// Based on UEFI Specification 2.10. No gnu-efi or EDK2 dependency.
// Compiled with --target=x86_64-unknown-windows so MS x64 ABI is the default —
// all EFI function pointers use the correct calling convention automatically.

// ---------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------
typedef unsigned char       UINT8;
typedef unsigned short      UINT16;
typedef unsigned int        UINT32;
typedef unsigned long long  UINT64;
typedef signed long long    INT64;
typedef unsigned long long  UINTN;
typedef signed long long    INTN;
typedef UINT8               BOOLEAN;
typedef UINT16              CHAR16;
typedef void                VOID;
typedef UINT64              EFI_STATUS;
typedef UINT64              EFI_PHYSICAL_ADDRESS;
typedef UINT64              EFI_VIRTUAL_ADDRESS;

typedef void* EFI_HANDLE;
typedef void* EFI_EVENT;

#define TRUE  ((BOOLEAN)1)
#define FALSE ((BOOLEAN)0)
#define NULL  ((void*)0)

// ---------------------------------------------------------------------------
// Status codes
// ---------------------------------------------------------------------------
#define EFI_ERROR_BIT             (1ULL << 63)
#define EFI_SUCCESS               0ULL
#define EFI_LOAD_ERROR            (EFI_ERROR_BIT | 1)
#define EFI_INVALID_PARAMETER     (EFI_ERROR_BIT | 2)
#define EFI_UNSUPPORTED           (EFI_ERROR_BIT | 3)
#define EFI_BAD_BUFFER_SIZE       (EFI_ERROR_BIT | 4)
#define EFI_BUFFER_TOO_SMALL      (EFI_ERROR_BIT | 5)
#define EFI_NOT_FOUND             (EFI_ERROR_BIT | 14)
#define EFI_OUT_OF_RESOURCES      (EFI_ERROR_BIT | 9)
#define EFI_ERROR(s)              (((EFI_STATUS)(s)) & EFI_ERROR_BIT)

// ---------------------------------------------------------------------------
// GUID
// ---------------------------------------------------------------------------
typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

// ---------------------------------------------------------------------------
// Memory types
// ---------------------------------------------------------------------------
typedef enum {
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
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32               Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    UINT64               NumberOfPages;   // 4-KB pages
    UINT64               Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

// ---------------------------------------------------------------------------
// Table header
// ---------------------------------------------------------------------------
typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

// ---------------------------------------------------------------------------
// Simple text output (for early console printing)
// ---------------------------------------------------------------------------
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void*      Reset;
    EFI_STATUS (*OutputString)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, CHAR16*);
    void*      TestString;
    void*      QueryMode;
    void*      SetMode;
    void*      SetAttribute;
    void*      ClearScreen;
    void*      SetCursorPosition;
    void*      EnableCursor;
    void*      Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

// ---------------------------------------------------------------------------
// Boot services (only the subset we use)
// ---------------------------------------------------------------------------
typedef struct {
    EFI_TABLE_HEADER Hdr;

    // Task priority (unused)
    void* RaiseTPL;
    void* RestoreTPL;

    // Memory allocation
    EFI_STATUS (*AllocatePages)(EFI_ALLOCATE_TYPE, EFI_MEMORY_TYPE, UINTN Pages,
                                 EFI_PHYSICAL_ADDRESS* Memory);
    EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
    EFI_STATUS (*GetMemoryMap)(UINTN* MapSize, EFI_MEMORY_DESCRIPTOR* Map,
                                UINTN* MapKey, UINTN* DescriptorSize,
                                UINT32* DescriptorVersion);
    EFI_STATUS (*AllocatePool)(EFI_MEMORY_TYPE, UINTN Size, VOID** Buffer);
    EFI_STATUS (*FreePool)(VOID* Buffer);

    // Events (unused)
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
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE, EFI_GUID*, VOID**);
    void* Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;

    // Image services
    void* LoadImage;
    void* StartImage;
    void* Exit;
    void* UnloadImage;
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey);

    // Misc
    void* GetNextMonotonicCount;
    void* Stall;
    void* SetWatchdogTimer;
    void* ConnectController;
    void* DisconnectController;

    // Open / close protocol
    EFI_STATUS (*OpenProtocol)(EFI_HANDLE, EFI_GUID*, VOID**, EFI_HANDLE, EFI_HANDLE, UINT32);
    void* CloseProtocol;
    void* OpenProtocolInformation;
    void* ProtocolsPerHandle;
    void* LocateHandleBuffer;
    EFI_STATUS (*LocateProtocol)(EFI_GUID* Protocol, VOID* Registration, VOID** Interface);
    void* InstallMultipleProtocolInterfaces;
    void* UninstallMultipleProtocolInterfaces;

    // CRC + memory utils
    void* CalculateCrc32;
    void* CopyMem;
    void* SetMem;
    void* CreateEventEx;
} EFI_BOOT_SERVICES;

// ---------------------------------------------------------------------------
// System table
// ---------------------------------------------------------------------------
typedef struct {
    EFI_TABLE_HEADER                 Hdr;
    CHAR16*                          FirmwareVendor;
    UINT32                           FirmwareRevision;
    EFI_HANDLE                       ConsoleInHandle;
    VOID*                            ConIn;
    EFI_HANDLE                       ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE                       StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;
    VOID*                            RuntimeServices;
    EFI_BOOT_SERVICES*               BootServices;
    UINTN                            NumberOfTableEntries;
    VOID*                            ConfigurationTable;
} EFI_SYSTEM_TABLE;

// ---------------------------------------------------------------------------
// Graphics Output Protocol (for framebuffer)
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
    UINT32                              MaxMode;
    UINT32                              Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN                               SizeOfInfo;
    EFI_PHYSICAL_ADDRESS                FrameBufferBase;
    UINTN                               FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    void* QueryMode;
    void* SetMode;
    void* Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;
