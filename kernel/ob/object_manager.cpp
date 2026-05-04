// object_manager.cpp - MicroNT Object Manager (M1: minimal)

#include "../include/object.h"
#include "../include/memory.h"
#include "../include/debug.h"

namespace OB {

void Init() {
    // TODO(M4): Initialize handle tables, object namespace, type objects.
    KDBG_INFO("OB: Object manager initialized (M1 stub)");
}

ObjectHeader* GetHeader(void* object) {
    return reinterpret_cast<ObjectHeader*>(
        reinterpret_cast<u8*>(object) - sizeof(ObjectHeader));
}

void* AllocateObject(ObjectType type, usize body_size, const char* name) {
    usize total = sizeof(ObjectHeader) + body_size;
    auto* hdr = static_cast<ObjectHeader*>(KernelHeap::AllocZeroed(total));
    if (!hdr) return nullptr;

    hdr->Magic       = OB_MAGIC;
    hdr->Type        = type;
    hdr->RefCount    = 1;
    hdr->HandleCount = 0;

    if (name) {
        usize n = 0;
        while (name[n] && n < sizeof(hdr->Name) - 1) {
            hdr->Name[n] = name[n]; ++n;
        }
        hdr->Name[n] = 0;
    }

    return reinterpret_cast<void*>(
        reinterpret_cast<u8*>(hdr) + sizeof(ObjectHeader));
}

void ReferenceObject(void* object) {
    auto* hdr = GetHeader(object);
    // TODO(M4): atomic increment
    ++hdr->RefCount;
}

void DereferenceObject(void* object) {
    auto* hdr = GetHeader(object);
    // TODO(M4): atomic decrement + free on zero
    if (--hdr->RefCount == 0) {
        KDBG_TRACE("OB: object '%s' refcount hit 0", hdr->Name);
        // KernelHeap::Free(hdr);  // enable when Free is implemented
    }
}

NTSTATUS OpenObjectByName(const char* /*name*/, ObjectType /*type*/, void** /*out*/) {
    // TODO(M4): Namespace lookup
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS InsertObject(void* /*object*/, HANDLE* /*out_handle*/) {
    // TODO(M4): Handle table
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS CloseHandle(HANDLE /*handle*/) {
    // TODO(M4): Handle table
    return STATUS_NOT_IMPLEMENTED;
}

void* ReferenceObjectByHandle(HANDLE /*handle*/, ObjectType /*expected_type*/) {
    // TODO(M4): Handle table lookup
    return nullptr;
}

} // namespace OB
