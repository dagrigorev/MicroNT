// virtual_memory.cpp - MicroNT M3 Virtual Memory Manager
//
// Design (Option B - kernel stays at 0x100000, identity-mapped):
//
//   The UEFI bootloader set up a PML4 that identity-maps 0-4 GB using
//   2 MB huge pages (4 PD tables, each with 512 x 0x83 entries).
//   We reuse that CR3 and EXTEND it:
//     - Do NOT touch the existing 0-4 GB huge-page region.
//     - For new virtual addresses returned by AllocKernelVA (>= 4 GB,
//       upper canonical half), create fresh PML4/PDPT/PD/PT entries.
//
//   Because physical addresses are identity-mapped, all physical
//   pointers are directly dereferenceable as virtual pointers.

#include "../include/memory.h"
#include "../include/debug.h"
#include "../include/hal.h"

namespace VMM {

// ============================================================
// Globals
// ============================================================
static u64 s_pml4_phys = 0;

// Kernel VA bump allocator: starts at 0xFFFF_FF80_0000_0000
// (PML4 index 511, upper canonical half, 512 GB available)
static u64 s_kvirt_next = 0xFFFF'FF80'0000'0000ULL;

// ============================================================
// Helpers
// ============================================================
static u64 pml4_idx(u64 v) { return (v >> 39) & 0x1FF; }
static u64 pdpt_idx(u64 v) { return (v >> 30) & 0x1FF; }
static u64 pd_idx  (u64 v) { return (v >> 21) & 0x1FF; }
static u64 pt_idx  (u64 v) { return (v >> 12) & 0x1FF; }

// Walk a page-table entry: return pointer to child table, or allocate one.
// Returns nullptr if the entry is a huge page (PS=1) or allocation fails.
static u64* get_or_alloc(u64* entry_ptr, bool alloc) {
    u64 e = *entry_ptr;
    if (e & PTE_PRESENT) {
        if (e & PTE_PS) return nullptr;   // huge page - cannot descend
        return reinterpret_cast<u64*>(e & PTE_ADDR_MASK);
    }
    if (!alloc) return nullptr;

    u64 phys = PMM::AllocPage();
    if (!phys) {
        KDBG_ERROR("VMM: PMM::AllocPage() failed for PT");
        return nullptr;
    }
    // Zero the new table (identity map: phys == virt for < 4 GB)
    auto* tbl = reinterpret_cast<u64*>(phys);
    for (int i = 0; i < 512; ++i) tbl[i] = 0;

    // Intermediate entries: present + writable + user
    // (user bit on intermediates lets both ring-0 and ring-3 traverse)
    *entry_ptr = phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    return tbl;
}

// ============================================================
// Public API
// ============================================================

void Init() {
    // Read PML4 physical address from CR3
    // CR3 bits [51:12] = page-directory base (bits [11:0] are flags)
    s_pml4_phys = HAL::ReadCr3() & PTE_ADDR_MASK;
    KDBG_INFO("VMM: PML4 at phys 0x%llx", s_pml4_phys);
    KDBG_INFO("VMM: kernel VA allocator base 0xFFFF_FF80_0000_0000");
}

bool MapPage(u64 virt, u64 phys, u64 flags) {
    auto* pml4 = reinterpret_cast<u64*>(s_pml4_phys);

    u64* pdpt = get_or_alloc(&pml4[pml4_idx(virt)], true);
    if (!pdpt) { KDBG_ERROR("VMM::MapPage: PDPT alloc failed"); return false; }

    u64* pd = get_or_alloc(&pdpt[pdpt_idx(virt)], true);
    if (!pd) { KDBG_ERROR("VMM::MapPage: PD alloc failed"); return false; }

    u64* pt = get_or_alloc(&pd[pd_idx(virt)], true);
    if (!pt) {
        KDBG_ERROR("VMM::MapPage: PD entry 0x%llx is a huge page or alloc failed",
                   pd[pd_idx(virt)]);
        return false;
    }

    pt[pt_idx(virt)] = (phys & PTE_ADDR_MASK) | (flags | PTE_PRESENT);
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
    return true;
}

void UnmapPage(u64 virt) {
    auto* pml4 = reinterpret_cast<u64*>(s_pml4_phys);

    u64 e1 = pml4[pml4_idx(virt)];
    if (!(e1 & PTE_PRESENT)) return;

    auto* pdpt = reinterpret_cast<u64*>(e1 & PTE_ADDR_MASK);
    u64 e2 = pdpt[pdpt_idx(virt)];
    if (!(e2 & PTE_PRESENT) || (e2 & PTE_PS)) return;

    auto* pd = reinterpret_cast<u64*>(e2 & PTE_ADDR_MASK);
    u64 e3 = pd[pd_idx(virt)];
    if (!(e3 & PTE_PRESENT) || (e3 & PTE_PS)) return;

    auto* pt = reinterpret_cast<u64*>(e3 & PTE_ADDR_MASK);
    pt[pt_idx(virt)] = 0;
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

u64 V2P(u64 virt) {
    auto* pml4 = reinterpret_cast<u64*>(s_pml4_phys);

    u64 e1 = pml4[pml4_idx(virt)];
    if (!(e1 & PTE_PRESENT)) return 0;

    auto* pdpt = reinterpret_cast<u64*>(e1 & PTE_ADDR_MASK);
    u64 e2 = pdpt[pdpt_idx(virt)];
    if (!(e2 & PTE_PRESENT)) return 0;
    if (e2 & PTE_PS)  // 1 GB huge page
        return (e2 & 0xFFFFFFC0000000ULL) | (virt & 0x3FFFFFFF);

    auto* pd = reinterpret_cast<u64*>(e2 & PTE_ADDR_MASK);
    u64 e3 = pd[pd_idx(virt)];
    if (!(e3 & PTE_PRESENT)) return 0;
    if (e3 & PTE_PS)  // 2 MB huge page
        return (e3 & 0xFFFFFFFE00000ULL) | (virt & 0x1FFFFF);

    auto* pt = reinterpret_cast<u64*>(e3 & PTE_ADDR_MASK);
    u64 pte = pt[pt_idx(virt)];
    if (!(pte & PTE_PRESENT)) return 0;
    return (pte & PTE_ADDR_MASK) | (virt & 0xFFF);
}

u64 AllocKernelVA(usize pages) {
    u64 va = s_kvirt_next;
    s_kvirt_next += static_cast<u64>(pages) * PAGE_SIZE;
    return va;
}

bool HandlePageFault(u64 cr2, u32 error_code) {
    // M3: no demand-zero yet - return false to let caller panic.
    // TODO(M5): demand-zero for user stacks, copy-on-write, etc.
    UNUSED(cr2);
    UNUSED(error_code);
    return false;
}

} // namespace VMM

// ============================================================
// C-linkage bridge for idt.cpp (avoids including memory.h
// from idt.cpp which would create a dependency cycle with
// the operator new/delete definitions in heap.cpp)
// ============================================================
extern "C" bool VmmHandlePageFault(u64 cr2, u32 error_code) {
    return VMM::HandlePageFault(cr2, error_code);
}

// ============================================================
// M6: Per-process address space
// ============================================================

namespace VMM {

u64 CreateUserPml4() {
    u64 phys = PMM::AllocPage();
    if (!phys) return 0;

    auto* new_pml4    = reinterpret_cast<u64*>(phys);
    auto* kernel_pml4 = reinterpret_cast<u64*>(s_pml4_phys);

    // Copy all 512 entries so the kernel (at 0x100000 = PML4[0]) stays mapped.
    // Then set PTE_USER on every present PML4 entry so that ring-3 page-table
    // walks can traverse the intermediate tables.  Kernel pages themselves do
    // NOT have PTE_USER in their PT entries, so ring-3 still faults on them.
    // Only explicitly user-mapped pages (PTE_USER in the PTE) are accessible.
    for (int i = 0; i < 512; ++i) {
        u64 e = kernel_pml4[i];
        if (e & PTE_PRESENT)
            e |= PTE_USER;  // allow ring-3 to traverse this PDPT
        new_pml4[i] = e;
    }

    KDBG_TRACE("VMM: CreateUserPml4 -> phys 0x%llx", phys);
    return phys;
}

bool MapPageInto(u64 pml4_phys, u64 virt, u64 phys, u64 flags) {
    auto* pml4 = reinterpret_cast<u64*>(pml4_phys);

    u64* pdpt = get_or_alloc(&pml4[pml4_idx(virt)], true);
    if (!pdpt) return false;
    u64* pd = get_or_alloc(&pdpt[pdpt_idx(virt)], true);
    if (!pd) return false;
    u64* pt = get_or_alloc(&pd[pd_idx(virt)], true);
    if (!pt) return false;

    pt[pt_idx(virt)] = (phys & PTE_ADDR_MASK) | (flags | PTE_PRESENT);
    // Note: no invlpg - this PML4 may not be active
    return true;
}

void SwitchAddressSpace(u64 cr3_phys) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3_phys) : "memory");
}

u64 TranslateInPml4(u64 pml4_phys, u64 virt) {
    // 4-level page walk using explicit PML4 (identity-mapped phys == virt for <4GB)
    auto* pml4 = reinterpret_cast<u64*>(pml4_phys);
    u64 e1 = pml4[pml4_idx(virt)];
    if (!(e1 & PTE_PRESENT)) return 0;
    if (e1 & PTE_PS) return 0; // huge page at PML4 level (unsupported)

    auto* pdpt = reinterpret_cast<u64*>(e1 & PTE_ADDR_MASK);
    u64 e2 = pdpt[pdpt_idx(virt)];
    if (!(e2 & PTE_PRESENT)) return 0;
    if (e2 & PTE_PS) // 1 GB huge page
        return (e2 & 0xFFFFFFC0000000ULL) | (virt & 0x3FFFFFFF);

    auto* pd = reinterpret_cast<u64*>(e2 & PTE_ADDR_MASK);
    u64 e3 = pd[pd_idx(virt)];
    if (!(e3 & PTE_PRESENT)) return 0;
    if (e3 & PTE_PS) // 2 MB huge page
        return (e3 & 0xFFFFFFFE00000ULL) | (virt & 0x1FFFFF);

    auto* pt = reinterpret_cast<u64*>(e3 & PTE_ADDR_MASK);
    u64 pte = pt[pt_idx(virt)];
    if (!(pte & PTE_PRESENT)) return 0;
    return (pte & PTE_ADDR_MASK) | (virt & 0xFFF);
}

} // namespace VMM
