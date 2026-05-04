#pragma once
// object.h - MicroNT Object Manager (M4: full implementation)

#include "ntdef.h"
#include "ntstatus.h"
#include "spinlock.h"

// ============================================================
// Object type descriptor
// Registered once per subsystem type (process, thread, event, ...)
// ============================================================
struct ObType {
    char   Name[32];                   // e.g. "Process", "Thread", "Event"
    usize  BodySize;                   // size of the object body (after header)
    void (*OnClose)(void* body);       // called when last reference dropped (nullable)
    u32    Index;                      // assigned at registration (0 = unregistered)
};

// ============================================================
// Object header (precedes every managed object in memory)
// Body immediately follows this header.
// ============================================================
constexpr u32 OB_MAGIC = 0x4E544F42u;  // 'NTOB'

struct ObjectHeader {
    u32      Magic;          // OB_MAGIC
    ObType*  Type;           // registered type descriptor
    i32      RefCount;       // reference count (atomic-like via spinlock)
    i32      HandleCount;    // number of open handles to this object
    u32      Flags;
    char     Name[64];       // namespace path, or empty if anonymous
};

// ============================================================
// Handle table
// ============================================================
constexpr usize OB_HANDLE_TABLE_SIZE = 256;

struct HandleEntry {
    void*       Object;       // body pointer (null = free slot)
    ACCESS_MASK GrantedAccess;
};

struct HandleTable {
    HandleEntry entries[OB_HANDLE_TABLE_SIZE];
    Spinlock    lock;

    // Returns INVALID_HANDLE on failure
    HANDLE      Alloc(void* object, ACCESS_MASK access);
    NTSTATUS    Free(HANDLE handle);
    HandleEntry* Lookup(HANDLE handle);  // returns null if invalid/closed
};

// ============================================================
// Object namespace
// Simple fixed-size table: full path -> object body pointer.
// Paths use backslash separator: \ObjectTypes\Process
// ============================================================
constexpr usize OB_NAMESPACE_SIZE = 128;

struct NamespaceEntry {
    char  Path[128];
    void* Object;            // null = free slot
};

// ============================================================
// Object Manager API
// ============================================================
namespace OB {

// ---- Initialization ----------------------------------------
void Init();

// ---- Type registry -----------------------------------------
// Register a new object type. Returns STATUS_SUCCESS or error.
NTSTATUS CreateType(const char* name, usize body_size,
                    void (*on_close)(void*), ObType** out_type);
ObType*  LookupType(const char* name);

// ---- Object lifecycle --------------------------------------
// Allocate object header + body (body_size from type).
// Returns pointer to BODY (header is before it in memory).
void*    AllocateObject(ObType* type, const char* namespace_path = nullptr);

// Manual size variant (for objects without a registered type, M1 compat)
void*    AllocateObjectRaw(u32 legacy_type_id, usize body_size,
                            const char* name = nullptr);

ObjectHeader* GetHeader(void* object);

void     ReferenceObject(void* object);
// Decrement refcount; calls type->OnClose and frees when it hits 0
void     DereferenceObject(void* object);

// ---- Kernel handle table -----------------------------------
// Insert object into the kernel handle table.
// Increments HandleCount and RefCount.
NTSTATUS InsertObject(void* object, ACCESS_MASK access, HANDLE* out_handle);

// Decrement HandleCount; calls DereferenceObject.
NTSTATUS CloseHandle(HANDLE handle);

// Look up object by handle.
// expected_type = nullptr means accept any type.
// On success: increments RefCount, caller must DereferenceObject when done.
NTSTATUS ReferenceObjectByHandle(HANDLE handle, ObType* expected_type,
                                  ACCESS_MASK desired_access, void** out_object);

// ---- Object namespace --------------------------------------
// Insert a named object. Path must start with '\'.
NTSTATUS InsertObjectByName(void* object, const char* path);

// Remove a named object from the namespace (does NOT deref).
void     RemoveObjectByName(const char* path);

// Look up by name. expected_type = nullptr accepts any.
// On success: increments RefCount.
NTSTATUS LookupObjectByName(const char* path, ObType* expected_type,
                              void** out_object);

// ---- Diagnostics -------------------------------------------
void     DumpStats();

} // namespace OB
