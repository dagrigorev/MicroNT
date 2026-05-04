#pragma once
// object.h - MicroNT Object Manager interface

#include "ntdef.h"
#include "ntstatus.h"

// ============================================================
// Object type IDs
// ============================================================
enum class ObjectType : u32 {
    Unknown   = 0,
    Directory = 1,
    Process   = 2,
    Thread    = 3,
    Event     = 4,
    Section   = 5,
    File      = 6,
    Device    = 7,
    Console   = 8,
    Mutex     = 9,
};

// ============================================================
// Object header (prepended to every kernel object)
// ============================================================
struct ObjectHeader {
    u32        Magic;          // 0x4E544F42 'NTOB'
    ObjectType Type;
    i32        RefCount;       // atomic in future
    i32        HandleCount;
    u32        Flags;
    char       Name[64];       // optional, null-terminated
};

constexpr u32 OB_MAGIC = 0x4E544F42;

// ============================================================
// Object Manager
// ============================================================
namespace OB {

void Init();

// Increment/decrement reference count
void  ReferenceObject(void* object);
void  DereferenceObject(void* object);

// Get the header for an object pointer
ObjectHeader* GetHeader(void* object);

// Object body is immediately after the header
template<typename T>
T* GetBody(ObjectHeader* hdr) {
    return reinterpret_cast<T*>(
        reinterpret_cast<u8*>(hdr) + sizeof(ObjectHeader));
}

// Allocate a new object with an embedded header
void* AllocateObject(ObjectType type, usize body_size, const char* name = nullptr);

// TODO(M4): Handle table, namespace lookup
// For M1 these are stubs returning STATUS_NOT_IMPLEMENTED.
NTSTATUS OpenObjectByName(const char* name, ObjectType type, void** out);
NTSTATUS InsertObject(void* object, HANDLE* out_handle);
NTSTATUS CloseHandle(HANDLE handle);
void*    ReferenceObjectByHandle(HANDLE handle, ObjectType expected_type);

} // namespace OB
