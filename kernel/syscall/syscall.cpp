// syscall.cpp - MicroNT M6 SYSCALL setup and dispatch

#include "../include/hal.h"
#include "../include/debug.h"
#include "../include/process.h"
#include "../include/ntstatus.h"

// ============================================================
// MSR addresses
// ============================================================
constexpr u32 MSR_EFER   = 0xC0000080;  // Extended Feature Enable Register
constexpr u32 MSR_STAR   = 0xC0000081;  // SYSCALL target CS/SS + SYSRET CS/SS
constexpr u32 MSR_LSTAR  = 0xC0000082;  // 64-bit SYSCALL target RIP
constexpr u32 MSR_SFMASK = 0xC0000084;  // RFLAGS bits to clear on SYSCALL

static u64 RdMsr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (u64)lo | ((u64)hi << 32);
}
static void WrMsr(u32 msr, u64 val) {
    __asm__ volatile("wrmsr" :: "c"(msr),
                               "a"((u32)(val & 0xFFFFFFFF)),
                               "d"((u32)(val >> 32)));
}

// ============================================================
// Syscall numbers (M6)
// ============================================================
constexpr u64 NT_TEST_SYSCALL     = 0;
constexpr u64 NT_TERMINATE_THREAD = 1;
constexpr u64 NT_WRITE_CONSOLE    = 2;
constexpr u64 NT_TEST_PE          = 3;   // M7: set g_m7_pe_ok flag

// ============================================================
// Completion signal (checked by kernel_main M6 test)
// ============================================================
volatile u32 g_m6_syscall_ok = 0;
volatile u32 g_m7_pe_ok      = 0;

// Assembly entry point (syscall_entry.asm)
extern "C" void syscall_entry();

// ============================================================
// C-level dispatcher (called from syscall_entry.asm as KiSystemCall)
// ============================================================
extern "C" u64 KiSystemCall(u64 number, u64 a1, u64 a2,
                             u64 a3, u64 a4, u64 a5) {
    UNUSED(a2); UNUSED(a3); UNUSED(a4); UNUSED(a5);
    switch (number) {
    case NT_TEST_SYSCALL:
        KDBG_INFO("SYSCALL: NtTestSyscall(a1=0x%llx) - user mode reached kernel!", a1);
        g_m6_syscall_ok = 1;
        return (u64)STATUS_SUCCESS;

    case NT_TEST_PE:
        KDBG_INFO("SYSCALL: NtTestPe(marker=0x%llx) - PE loaded and executing!", a1);
        g_m7_pe_ok = static_cast<u32>(a1);  // store marker for kernel to check
        return (u64)STATUS_SUCCESS;

    case NT_TERMINATE_THREAD:
        KDBG_INFO("SYSCALL: NtTerminateThread(exit=%lld)", (i64)a1);
        PS::TerminateCurrentThread(static_cast<i32>(a1));
        // never returns
        return (u64)STATUS_SUCCESS;

    case NT_WRITE_CONSOLE:
        // TODO(M7): validate user pointer, copy string, write to console
        KDBG_TRACE("SYSCALL: NtWriteConsole stub");
        return (u64)STATUS_SUCCESS;

    default:
        KDBG_WARN("SYSCALL: unknown number %llu (a1=0x%llx)", number, a1);
        return (u64)STATUS_INVALID_SYSTEM_SERVICE;
    }
}

namespace SYSCALL {

void Init() {
    // Enable SYSCALL/SYSRET in EFER (SCE = bit 0)
    WrMsr(MSR_EFER, RdMsr(MSR_EFER) | 1u);

    // STAR:
    //   [63:48] = 0x10 -> SYSRET  CS=0x10+16|3=0x23(ucode) SS=0x10+8|3=0x1B(udata)
    //   [47:32] = 0x08 -> SYSCALL CS=0x08(kcode)            SS=0x10(kdata)
    WrMsr(MSR_STAR, (0x10ULL << 48) | (0x08ULL << 32));

    // LSTAR: kernel-mode syscall handler
    WrMsr(MSR_LSTAR, reinterpret_cast<u64>(syscall_entry));

    // SFMASK: RFLAGS bits to clear on SYSCALL entry
    //   IF (bit 9): disable interrupts until we switch to kernel stack
    //   TF (bit 8): no single-step tracing
    //   DF (bit 10): clear direction flag
    WrMsr(MSR_SFMASK, (1u << 9) | (1u << 8) | (1u << 10));

    KDBG_INFO("SYSCALL: LSTAR=0x%llx STAR=0x%llx",
              reinterpret_cast<u64>(syscall_entry),
              (0x10ULL << 48) | (0x08ULL << 32));
}

} // namespace SYSCALL
