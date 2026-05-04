// heap.cpp - MicroNT M1 bump-allocator kernel heap

#include "../include/memory.h"
#include "../include/debug.h"

namespace KernelHeap {

namespace {
    static u64   s_base      = 0;
    static usize s_size      = 0;
    static usize s_used      = 0;
}

void Init(u64 base, usize size) {
    s_base = base;
    s_size = size;
    s_used = 0;
    // Zero the heap
    for (usize i = 0; i < size; ++i)
        reinterpret_cast<u8*>(base)[i] = 0;
    KDBG_INFO("KernelHeap: base=0x%llx size=%llu KB",
        base, (u64)(size / 1024));
}

void* Alloc(usize size, usize align) {
    usize cur = AlignUp(s_base + s_used, align) - s_base;
    if (cur + size > s_size) {
        KDBG_ERROR("KernelHeap: out of memory (need %llu, have %llu)",
            (u64)size, (u64)(s_size - cur));
        return nullptr;
    }
    void* ptr = reinterpret_cast<void*>(s_base + cur);
    s_used = cur + size;
    return ptr;
}

void* AllocZeroed(usize size, usize align) {
    void* p = Alloc(size, align);
    if (p) {
        for (usize i = 0; i < size; ++i)
            reinterpret_cast<u8*>(p)[i] = 0;
    }
    return p;
}

void Free(void* /*ptr*/) {
    // TODO(M3): implement free with a proper allocator
}

usize Used()      { return s_used; }
usize Remaining() { return s_size - s_used; }

} // namespace KernelHeap

// ============================================================
// Kernel new/delete — backed by KernelHeap
// ============================================================
void* operator new(usize size)   { return KernelHeap::Alloc(size); }
void* operator new[](usize size) { return KernelHeap::Alloc(size); }
void  operator delete(void* p)   noexcept { KernelHeap::Free(p); }
void  operator delete[](void* p) noexcept { KernelHeap::Free(p); }
void  operator delete(void* p, usize) noexcept { KernelHeap::Free(p); }
void  operator delete[](void* p, usize) noexcept { KernelHeap::Free(p); }
