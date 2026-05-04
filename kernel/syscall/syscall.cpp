// syscall.cpp - MicroNT syscall layer (M1: stub)
// TODO(M6): Implement SYSCALL/SYSRET entry point and dispatch table.

#include "../include/io.h"
#include "../include/debug.h"
#include "../include/hal.h"

// MSR addresses for SYSCALL/SYSRET
constexpr u32 MSR_EFER  = 0xC0000080;
[[maybe_unused]] constexpr u32 MSR_STAR  = 0xC0000081;
[[maybe_unused]] constexpr u32 MSR_LSTAR = 0xC0000082;
[[maybe_unused]] constexpr u32 MSR_FMASK = 0xC0000084;

// EFER.SCE bit
constexpr u64 EFER_SCE = (1ULL << 0);

namespace SYSCALL {

void Init() {
    // M1: Just verify SYSCALL is available via CPUID and enable SCE in EFER.
    // The actual entry point and dispatch table will be wired up in M6.

    u32 eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000001), "c"(0));

    if (!(edx & (1u << 11))) {
        KDBG_WARN("SYSCALL: SYSCALL/SYSRET not supported on this CPU");
        return;
    }

    // Enable SCE in EFER
    u64 efer = HAL::ReadMsr(MSR_EFER);
    HAL::WriteMsr(MSR_EFER, efer | EFER_SCE);

    // TODO(M6): Set STAR (segment selectors), LSTAR (entry point), FMASK (RFLAGS mask)
    // HAL::WriteMsr(MSR_STAR,  ((u64)0x0018 << 48) | ((u64)0x0008 << 32));
    // HAL::WriteMsr(MSR_LSTAR, (u64)&syscall_entry);
    // HAL::WriteMsr(MSR_FMASK, 0x200);  // clear IF on syscall entry

    KDBG_INFO("SYSCALL: layer initialized (M1 stub)");
}

} // namespace SYSCALL
