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
// Virtual Memory Manager
// ============================================================
namespace VMM {

// Page table entry flags
constexpr u64 PTE_PRESENT  = (1ULL << 0);
constexpr u64 PTE_WRITABLE = (1ULL << 1);
constexpr u64 PTE_USER     = (1ULL << 2);
constexpr u64 PTE_PS       = (1ULL << 7);   // huge page (PD or PDPT entry)
constexpr u64 PTE_NX       = (1ULL << 63);  // no-execute (requires EFER.NXE)

// Physical address bits in a PTE
constexpr u64 PTE_ADDR_MASK = 0x000FFFFFFFFFF000ULL;

// Init: reads CR3, records PML4 base, sets up kernel VA allocator
void Init();

// Map a single 4 KB page. Returns false if PT allocation fails.
// Caller must not map addresses already covered by 2MB huge pages
// (i.e. don't use this for 0-4 GB range covered by the UEFI identity map).
bool MapPage(u64 virt, u64 phys, u64 flags = PTE_PRESENT | PTE_WRITABLE);

// Unmap a single 4 KB page and flush TLB entry.
void UnmapPage(u64 virt);
void UnmapPageFrom(u64 pml4_phys, u64 virt);  // clear PTE in given PML4

// Translate virtual -> physical (0 = not mapped or huge-page addressed).
u64  V2P(u64 virt);

// Kernel virtual address allocator.
// Returns a range of 'pages' contiguous virtual addresses above 4 GB
// (starting at 0xFFFF_FF80_0000_0000 on first call).
// Caller must MapPage() each page before accessing.
u64  AllocKernelVA(usize pages);

// Called from #PF exception handler before panicking.
// Returns true if the fault was handled (demand-zero etc.).
bool HandlePageFault(u64 cr2, u32 error_code);

// ---- M6: per-process address space -------------------------
// Create a new PML4 for a user process:
//   - Lower half (PML4[0..255]) zeroed (fresh user address space)
//   - Upper half (PML4[256..511]) copied from kernel PML4 (shared kernel map)
// Returns physical address of new PML4, or 0 on failure.
u64  CreateUserPml4();

// Windows compatibility: split the 2 MB identity huge page covering
// 0x7FFE0000 (in the shared low page-directory) and map a single 4 KB page
// there, read-only + user. Because every user PML4 shares this page-directory,
// the page becomes visible at 0x7FFE0000 in every process at once -- exactly
// how Windows exposes KUSER_SHARED_DATA. Call once after VMM::Init.
bool MapSharedUserData(u64 phys);

// Like MapPage but operates on an explicit PML4 (for mapping into a
// process that is not currently active).  No invlpg needed.
bool MapPageInto(u64 pml4_phys, u64 virt, u64 phys, u64 flags);

// Switch the active address space (write CR3).
void SwitchAddressSpace(u64 cr3_phys);

// Walk an explicit PML4 to translate virtual -> physical.
// Returns 0 if the page is not mapped or is a huge page (>4KB).
u64  TranslateInPml4(u64 pml4_phys, u64 virt);

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
