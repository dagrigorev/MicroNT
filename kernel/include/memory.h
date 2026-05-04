#pragma once
// memory.h - MicroNT memory manager interface

#include "ntdef.h"
#include "bootinfo.h"
#include "ntstatus.h"

// ============================================================
// Physical Memory Manager
// ============================================================
namespace PMM {

void Init(MicroNTBootInfo* boot_info);

// Allocate/free physical pages (returns physical addresses)
u64  AllocPage();
void FreePage(u64 phys_addr);
u64  AllocPages(usize count);   // contiguous pages

// Statistics
usize TotalPages();
usize FreePages();
usize UsedPages();

} // namespace PMM

// ============================================================
// Kernel Heap  (M1: bump allocator, no free)
// ============================================================
namespace KernelHeap {

void  Init(u64 base, usize size);
void* Alloc(usize size, usize align = 16);
void* AllocZeroed(usize size, usize align = 16);
void  Free(void* ptr);   // TODO(M3): implement proper free

usize Used();
usize Remaining();

} // namespace KernelHeap

// ============================================================
// Virtual Memory Manager  (M1: stub, identity map in effect)
// ============================================================
namespace VMM {

void Init();

// TODO(M3): MapPage, UnmapPage, page fault handler
// For M1 the identity map from boot.asm is used directly.

} // namespace VMM

// ============================================================
// Kernel new/delete (backed by KernelHeap)
// ============================================================
void* operator new(usize size);
void* operator new[](usize size);
void  operator delete(void* ptr) noexcept;
void  operator delete[](void* ptr) noexcept;
void  operator delete(void* ptr, usize) noexcept;
void  operator delete[](void* ptr, usize) noexcept;

// Placement new (no allocation, required by C++ standard)
inline void* operator new(usize, void* p)  noexcept { return p; }
inline void* operator new[](usize, void* p) noexcept { return p; }
