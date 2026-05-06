#pragma once
// process.h - MicroNT M5-M15: Process and Thread management

#include "ntdef.h"
#include "ntstatus.h"

// ============================================================
// Thread states
// ============================================================
enum class ThreadState : u32 {
    READY      = 0,
    RUNNING    = 1,
    BLOCKED    = 2,
    TERMINATED = 3,
};

// ============================================================
// Thread priority levels  (M15)
// ============================================================
constexpr u32 THREAD_PRIORITY_LOW    = 0;
constexpr u32 THREAD_PRIORITY_NORMAL = 1;
constexpr u32 THREAD_PRIORITY_HIGH   = 2;
constexpr u32 THREAD_PRIORITY_COUNT  = 3;

// ============================================================
// Free VA region — per-process free list entry  (M15)
// ============================================================
struct FreeRegion { u64 va; u32 pages; };
constexpr usize FREE_LIST_SLOTS = 16;

// ============================================================
// KThread - Kernel Thread Control Block
//
// CRITICAL: KernelStackPtr MUST remain at offset 0.
// switch_context (switch.asm) uses [rdi+0] and [rsi+0] directly.
// ============================================================
struct KProcess;

struct KThread {
    u64         KernelStackPtr;   // offset 0: saved RSP (set by switch_context)

    u32         Tid;
    ThreadState State;
    u32         QuantumLeft;      // remaining ticks before preemption
    u32         Flags;

    // M15: scheduling priority (THREAD_PRIORITY_*)
    u32         Priority;

    KProcess*   Process;          // owning process

    u64         KernelStackBase;  // bottom of kernel stack allocation (phys)
    usize       KernelStackSize;  // size in bytes

    void*       EntryFn;          // original entry function (for kernel threads)
    void*       EntryArg;

    char        Name[32];

    // Intrusive doubly-linked list (ready queue)
    KThread*    Next;
    KThread*    Prev;

    // Singly-linked list for event waiter queues
    KThread*    WaitNext;

    // Sleep support (NtDelayExecution)
    u64         SleepUntil;   // PIT tick deadline; 0 = not sleeping
    KThread*    SleepNext;    // singly-linked sleep list

    // User-mode thread argument (passed as RDI via extended IRETQ frame)
    u64         UserArg;

    // M15: exception handler VA (0 = none registered)
    u64         ExceptionHandler;
};

// ============================================================
// KProcess - Kernel Process Control Block
// ============================================================
struct KProcess {
    u32         Pid;
    u32         Flags;
    u64         Cr3;              // physical address of PML4
    u64         UserHeapCursor;   // per-process bump allocator (init = 0x500000000)
    char        Name[32];
    i32         ExitStatus;

    // M15: per-process free list for NtFreeVirtualMemory
    FreeRegion  UserFreeList[FREE_LIST_SLOTS];
};

// ============================================================
// PS - Process/Thread creation
// ============================================================
namespace PS {

void     Init();

KProcess* CreateProcess(const char* name, u64 cr3 = 0);
void      DestroyProcess(KProcess* process);

KThread* CreateKernelThread(KProcess* process, const char* name,
                             void (*entry_fn)(void*), void* arg,
                             usize kernel_stack_size = 16384);

KThread* CreateUserThread(KProcess* process, const char* name,
                           u64 user_entry_va, u64 user_stack_va,
                           u64 user_arg = 0,
                           usize kernel_stack_size = 16384);

[[noreturn]] void TerminateCurrentThread(i32 exit_code = 0);

KProcess* SystemProcess();
KThread*  MainThread();

} // namespace PS

// ============================================================
// Sched - Priority scheduler  (M15: 3-level priority queues)
// ============================================================
namespace Sched {

constexpr u32 QUANTUM_TICKS = 5;

void     Init();
void     Start();
void     AddThread(KThread* t);
void     RemoveThread(KThread* t);
void     Tick();
void     Schedule();
KThread* CurrentThread();
bool     IsActive();
void     BlockCurrentThread();
void     UnblockThread(KThread* t);
void     Sleep(u32 ms);

} // namespace Sched

// ============================================================
// Assembly trampolines (switch.asm)
// ============================================================
extern "C" {
    void switch_context(KThread* prev, KThread* next);
    void kernel_thread_entry();
    void user_thread_entry();
}
