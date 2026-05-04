// object_manager.cpp - MicroNT Object Manager (M4)

#include "../include/object.h"
#include "../include/memory.h"
#include "../include/debug.h"
#include "../include/hal.h"

// ============================================================
// HandleTable implementation
// ============================================================

// Encode: HANDLE = (index + 1) cast to pointer (never null, never -1)
static inline HANDLE  index_to_handle(usize i) {
    return reinterpret_cast<HANDLE>((i + 1) * sizeof(void*));
}
static inline usize   handle_to_index(HANDLE h) {
    return reinterpret_cast<uptr>(h) / sizeof(void*) - 1;
}
static inline bool    handle_valid(HANDLE h) {
    if (!h || h == INVALID_HANDLE) return false;
    uptr v = reinterpret_cast<uptr>(h);
    if (v % sizeof(void*) != 0) return false;
    usize idx = v / sizeof(void*);
    return idx >= 1 && idx <= OB_HANDLE_TABLE_SIZE;
}

HANDLE HandleTable::Alloc(void* object, ACCESS_MASK access) {
    SpinlockGuard g(lock);
    for (usize i = 0; i < OB_HANDLE_TABLE_SIZE; ++i) {
        if (!entries[i].Object) {
            entries[i].Object       = object;
            entries[i].GrantedAccess = access;
            return index_to_handle(i);
        }
    }
    return INVALID_HANDLE;
}

NTSTATUS HandleTable::Free(HANDLE handle) {
    if (!handle_valid(handle)) return STATUS_INVALID_HANDLE;
    usize idx = handle_to_index(handle);
    SpinlockGuard g(lock);
    if (!entries[idx].Object) return STATUS_INVALID_HANDLE;
    entries[idx].Object       = nullptr;
    entries[idx].GrantedAccess = 0;
    return STATUS_SUCCESS;
}

HandleEntry* HandleTable::Lookup(HANDLE handle) {
    if (!handle_valid(handle)) return nullptr;
    usize idx = handle_to_index(handle);
    SpinlockGuard g(lock);
    if (!entries[idx].Object) return nullptr;
    return &entries[idx];
}

// ============================================================
// Internal string helpers (no <string.h>)
// ============================================================
static usize str_len(const char* s) {
    usize n = 0; while (s && s[n]) ++n; return n;
}
static bool str_eq(const char* a, const char* b) {
    if (!a || !b) return a == b;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void str_copy(char* dst, const char* src, usize max) {
    usize i = 0;
    while (i + 1 < max && src && src[i]) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

// ============================================================
// Module-level state
// ============================================================
namespace {

// Type registry (max 32 object types)
constexpr usize MAX_TYPES = 32;
static ObType       s_types[MAX_TYPES];
static u32          s_type_count = 0;
static Spinlock     s_type_lock;

// Global kernel handle table
static HandleTable  s_ktable;

// Global namespace
static NamespaceEntry s_namespace[OB_NAMESPACE_SIZE];
static Spinlock       s_ns_lock;

} // namespace

// ============================================================
// OB implementation
// ============================================================
namespace OB {

void Init() {
    // Zero handle table and namespace
    for (usize i = 0; i < OB_HANDLE_TABLE_SIZE; ++i) {
        s_ktable.entries[i].Object       = nullptr;
        s_ktable.entries[i].GrantedAccess = 0;
    }
    for (usize i = 0; i < OB_NAMESPACE_SIZE; ++i) {
        s_namespace[i].Path[0] = '\0';
        s_namespace[i].Object  = nullptr;
    }
    s_type_count = 0;
    KDBG_INFO("OB: Object manager initialized (M4)");
}

// ---- Type registry ----------------------------------------

NTSTATUS CreateType(const char* name, usize body_size,
                    void (*on_close)(void*), ObType** out_type) {
    if (!name || !out_type) return STATUS_INVALID_PARAMETER;

    SpinlockGuard g(s_type_lock);

    // Check for duplicate
    for (u32 i = 0; i < s_type_count; ++i) {
        if (str_eq(s_types[i].Name, name)) {
            *out_type = &s_types[i];
            return STATUS_SUCCESS;
        }
    }

    if (s_type_count >= MAX_TYPES) {
        KDBG_ERROR("OB: type registry full (%u types)", (u32)MAX_TYPES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ObType* t = &s_types[s_type_count];
    str_copy(t->Name, name, sizeof(t->Name));
    t->BodySize = body_size;
    t->OnClose  = on_close;
    t->Index    = ++s_type_count;

    KDBG_INFO("OB: registered type '%s' idx=%u size=%llu",
              t->Name, t->Index, (u64)body_size);
    *out_type = t;
    return STATUS_SUCCESS;
}

ObType* LookupType(const char* name) {
    if (!name) return nullptr;
    SpinlockGuard g(s_type_lock);
    for (u32 i = 0; i < s_type_count; ++i) {
        if (str_eq(s_types[i].Name, name)) return &s_types[i];
    }
    return nullptr;
}

// ---- Object lifecycle --------------------------------------

ObjectHeader* GetHeader(void* object) {
    if (!object) return nullptr;
    return reinterpret_cast<ObjectHeader*>(
        reinterpret_cast<u8*>(object) - sizeof(ObjectHeader));
}

void* AllocateObject(ObType* type, const char* namespace_path) {
    if (!type) return nullptr;
    usize total = sizeof(ObjectHeader) + type->BodySize;
    auto* hdr = static_cast<ObjectHeader*>(
        KernelHeap::AllocZeroed(total, alignof(ObjectHeader)));
    if (!hdr) return nullptr;

    hdr->Magic       = OB_MAGIC;
    hdr->Type        = type;
    hdr->RefCount    = 1;
    hdr->HandleCount = 0;
    hdr->Flags       = 0;
    if (namespace_path)
        str_copy(hdr->Name, namespace_path, sizeof(hdr->Name));

    void* body = reinterpret_cast<u8*>(hdr) + sizeof(ObjectHeader);

    if (namespace_path && namespace_path[0]) {
        NTSTATUS st = InsertObjectByName(body, namespace_path);
        if (!NT_SUCCESS(st)) {
            KDBG_WARN("OB: AllocateObject: namespace insert failed (0x%x)", st);
        }
    }
    return body;
}

void* AllocateObjectRaw(u32 /*legacy_type_id*/, usize body_size,
                         const char* name) {
    usize total = sizeof(ObjectHeader) + body_size;
    auto* hdr = static_cast<ObjectHeader*>(
        KernelHeap::AllocZeroed(total, alignof(ObjectHeader)));
    if (!hdr) return nullptr;
    hdr->Magic       = OB_MAGIC;
    hdr->Type        = nullptr;
    hdr->RefCount    = 1;
    hdr->HandleCount = 0;
    if (name) str_copy(hdr->Name, name, sizeof(hdr->Name));
    return reinterpret_cast<u8*>(hdr) + sizeof(ObjectHeader);
}

void ReferenceObject(void* object) {
    auto* hdr = GetHeader(object);
    if (!hdr || hdr->Magic != OB_MAGIC) return;
    // Simple increment (single CPU for M4 - M5 will add atomics)
    HAL::DisableInterrupts();
    ++hdr->RefCount;
    HAL::EnableInterrupts();
}

void DereferenceObject(void* object) {
    auto* hdr = GetHeader(object);
    if (!hdr || hdr->Magic != OB_MAGIC) return;

    HAL::DisableInterrupts();
    i32 ref = --hdr->RefCount;
    HAL::EnableInterrupts();

    if (ref <= 0) {
        KDBG_TRACE("OB: object '%s' (type=%s) refcount=0 -> freeing",
            hdr->Name,
            hdr->Type ? hdr->Type->Name : "raw");

        // Remove from namespace if named
        if (hdr->Name[0])
            RemoveObjectByName(hdr->Name);

        // Call type destructor
        if (hdr->Type && hdr->Type->OnClose) {
            void* body = reinterpret_cast<u8*>(hdr) + sizeof(ObjectHeader);
            hdr->Type->OnClose(body);
        }

        // Mark as dead and free (TODO(M3+): use VMM free when heap supports it)
        hdr->Magic = 0;
        // KernelHeap::Free(hdr);  // enable when bump allocator replaced
    }
}

// ---- Handle table ------------------------------------------

NTSTATUS InsertObject(void* object, ACCESS_MASK access, HANDLE* out_handle) {
    if (!object || !out_handle) return STATUS_INVALID_PARAMETER;

    auto* hdr = GetHeader(object);
    if (!hdr || hdr->Magic != OB_MAGIC) return STATUS_INVALID_PARAMETER;

    HANDLE h = s_ktable.Alloc(object, access);
    if (h == INVALID_HANDLE) {
        KDBG_ERROR("OB: handle table full");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    HAL::DisableInterrupts();
    ++hdr->HandleCount;
    ++hdr->RefCount;
    HAL::EnableInterrupts();

    *out_handle = h;
    return STATUS_SUCCESS;
}

NTSTATUS CloseHandle(HANDLE handle) {
    HandleEntry* e = s_ktable.Lookup(handle);
    if (!e) return STATUS_INVALID_HANDLE;

    void* object = e->Object;
    auto* hdr    = GetHeader(object);

    NTSTATUS st = s_ktable.Free(handle);
    if (!NT_SUCCESS(st)) return st;

    if (hdr && hdr->Magic == OB_MAGIC) {
        HAL::DisableInterrupts();
        --hdr->HandleCount;
        HAL::EnableInterrupts();
        DereferenceObject(object);
    }
    return STATUS_SUCCESS;
}

NTSTATUS ReferenceObjectByHandle(HANDLE handle, ObType* expected_type,
                                  ACCESS_MASK /*desired_access*/,
                                  void** out_object) {
    if (!out_object) return STATUS_INVALID_PARAMETER;

    HandleEntry* e = s_ktable.Lookup(handle);
    if (!e) return STATUS_INVALID_HANDLE;

    auto* hdr = GetHeader(e->Object);
    if (!hdr || hdr->Magic != OB_MAGIC) return STATUS_INVALID_HANDLE;

    if (expected_type && hdr->Type != expected_type)
        return STATUS_OBJECT_TYPE_MISMATCH;

    ReferenceObject(e->Object);
    *out_object = e->Object;
    return STATUS_SUCCESS;
}

// ---- Namespace ---------------------------------------------

NTSTATUS InsertObjectByName(void* object, const char* path) {
    if (!object || !path || path[0] != '\\') return STATUS_INVALID_PARAMETER;
    if (str_len(path) >= sizeof(s_namespace[0].Path))
        return STATUS_INVALID_PARAMETER;

    SpinlockGuard g(s_ns_lock);

    // Check for duplicate
    for (usize i = 0; i < OB_NAMESPACE_SIZE; ++i) {
        if (s_namespace[i].Object && str_eq(s_namespace[i].Path, path))
            return STATUS_ALREADY_EXISTS;
    }

    // Find free slot
    for (usize i = 0; i < OB_NAMESPACE_SIZE; ++i) {
        if (!s_namespace[i].Object) {
            str_copy(s_namespace[i].Path, path, sizeof(s_namespace[i].Path));
            s_namespace[i].Object = object;
            KDBG_TRACE("OB: namespace insert '%s'", path);
            return STATUS_SUCCESS;
        }
    }

    KDBG_ERROR("OB: namespace full (%u entries)", (u32)OB_NAMESPACE_SIZE);
    return STATUS_INSUFFICIENT_RESOURCES;
}

void RemoveObjectByName(const char* path) {
    if (!path) return;
    SpinlockGuard g(s_ns_lock);
    for (usize i = 0; i < OB_NAMESPACE_SIZE; ++i) {
        if (s_namespace[i].Object && str_eq(s_namespace[i].Path, path)) {
            s_namespace[i].Path[0] = '\0';
            s_namespace[i].Object  = nullptr;
            return;
        }
    }
}

NTSTATUS LookupObjectByName(const char* path, ObType* expected_type,
                              void** out_object) {
    if (!path || !out_object) return STATUS_INVALID_PARAMETER;

    SpinlockGuard g(s_ns_lock);
    for (usize i = 0; i < OB_NAMESPACE_SIZE; ++i) {
        if (!s_namespace[i].Object) continue;
        if (!str_eq(s_namespace[i].Path, path)) continue;

        void* obj = s_namespace[i].Object;
        auto* hdr = GetHeader(obj);
        if (!hdr || hdr->Magic != OB_MAGIC) continue;

        if (expected_type && hdr->Type != expected_type)
            return STATUS_OBJECT_TYPE_MISMATCH;

        ReferenceObject(obj);
        *out_object = obj;
        return STATUS_SUCCESS;
    }
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

void DumpStats() {
    u32 handles = 0;
    for (usize i = 0; i < OB_HANDLE_TABLE_SIZE; ++i)
        if (s_ktable.entries[i].Object) ++handles;

    u32 ns_used = 0;
    for (usize i = 0; i < OB_NAMESPACE_SIZE; ++i)
        if (s_namespace[i].Object) ++ns_used;

    Debug::Printf("[INFO ] OB: %u types  %u/%u handles  %u/%u namespace\r\n",
        s_type_count, handles, (u32)OB_HANDLE_TABLE_SIZE,
        ns_used, (u32)OB_NAMESPACE_SIZE);
}

} // namespace OB
