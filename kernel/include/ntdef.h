#pragma once
// ntdef.h - MicroNT core primitive types
// Original work. Not derived from Windows source or ReactOS.
//
// We intentionally avoid <stdint.h>/<stddef.h> so this header works
// under -nostdinc (freestanding kernel build).  Clang/GCC expose
// compiler-predefined macros like __UINT8_TYPE__ that give us the
// correct sized types without any include.

// ============================================================
// Fixed-width integer types (no <stdint.h> required)
// ============================================================
using u8    = __UINT8_TYPE__;
using u16   = __UINT16_TYPE__;
using u32   = __UINT32_TYPE__;
using u64   = __UINT64_TYPE__;
using i8    = __INT8_TYPE__;
using i16   = __INT16_TYPE__;
using i32   = __INT32_TYPE__;
using i64   = __INT64_TYPE__;
using usize = __SIZE_TYPE__;        // replaces size_t
using uptr  = __UINTPTR_TYPE__;    // replaces uintptr_t

// nullptr_t for freestanding (C++11 §18.2)
using nullptr_t = decltype(nullptr);

// ============================================================
// NT-inspired primitive types
// ============================================================
using NTSTATUS   = i32;
using HANDLE     = void*;
using ACCESS_MASK = u32;
using BOOLEAN    = u8;

// INVALID_HANDLE_VALUE equivalent
#define INVALID_HANDLE (reinterpret_cast<HANDLE>(static_cast<uptr>(-1LL)))
#define NULL_HANDLE     (static_cast<HANDLE>(nullptr))

// ============================================================
// UNICODE_STRING / ANSI_STRING
// ============================================================
struct UNICODE_STRING {
    u16  Length;        // bytes (not chars), excluding null
    u16  MaximumLength;
    u16* Buffer;
};

struct ANSI_STRING {
    u16  Length;
    u16  MaximumLength;
    char* Buffer;
};

// ============================================================
// LARGE_INTEGER
// ============================================================
union LARGE_INTEGER {
    struct { u32 LowPart; i32 HighPart; };
    i64 QuadPart;
};

union ULARGE_INTEGER {
    struct { u32 LowPart; u32 HighPart; };
    u64 QuadPart;
};

// ============================================================
// OBJECT_ATTRIBUTES
// ============================================================
struct OBJECT_ATTRIBUTES {
    u32              Length;
    HANDLE           RootDirectory;
    UNICODE_STRING*  ObjectName;
    u32              Attributes;
    void*            SecurityDescriptor;       // stub
    void*            SecurityQualityOfService; // stub
};

constexpr u32 OBJ_INHERIT            = 0x00000002;
constexpr u32 OBJ_PERMANENT          = 0x00000010;
constexpr u32 OBJ_EXCLUSIVE          = 0x00000020;
constexpr u32 OBJ_CASE_INSENSITIVE   = 0x00000040;
constexpr u32 OBJ_OPENIF             = 0x00000080;
constexpr u32 OBJ_OPENLINK           = 0x00000100;
constexpr u32 OBJ_KERNEL_HANDLE      = 0x00000200;

// ============================================================
// CLIENT_ID
// ============================================================
struct CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
};

// ============================================================
// IO_STATUS_BLOCK
// ============================================================
struct IO_STATUS_BLOCK {
    union { NTSTATUS Status; void* Pointer; };
    usize Information;
};

// ============================================================
// Common macros
// ============================================================
#define NT_SUCCESS(Status)      ((NTSTATUS)(Status) >= 0)
#define NT_INFORMATION(Status)  (((NTSTATUS)(Status) >> 30) == 1)
#define NT_WARNING(Status)      (((NTSTATUS)(Status) >> 30) == 2)
#define NT_ERROR(Status)        (((NTSTATUS)(Status) >> 30) == 3)

constexpr usize PAGE_SIZE       = 0x1000;
constexpr usize PAGE_SHIFT      = 12;
constexpr usize LARGE_PAGE_SIZE = 0x200000;

template<typename T>
constexpr T AlignUp(T value, T align) {
    return (value + align - 1) & ~(align - 1);
}

template<typename T>
constexpr T AlignDown(T value, T align) {
    return value & ~(align - 1);
}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define UNUSED(x)     ((void)(x))

// ============================================================
// Access rights
// ============================================================
constexpr ACCESS_MASK GENERIC_READ    = 0x80000000;
constexpr ACCESS_MASK GENERIC_WRITE   = 0x40000000;
constexpr ACCESS_MASK GENERIC_EXECUTE = 0x20000000;
constexpr ACCESS_MASK GENERIC_ALL     = 0x10000000;

constexpr ACCESS_MASK SYNCHRONIZE     = 0x00100000;
constexpr ACCESS_MASK READ_CONTROL    = 0x00020000;
