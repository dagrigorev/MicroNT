#pragma once
// process.h -- MicroNT M5-M17

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
// Priority levels  (M15)
// ============================================================
constexpr u32 THREAD_PRIORITY_LOW    = 0;
constexpr u32 THREAD_PRIORITY_NORMAL = 1;
constexpr u32 THREAD_PRIORITY_HIGH   = 2;
constexpr u32 THREAD_PRIORITY_COUNT  = 3;

// ============================================================
// Free VA region -- per-process allocator free list  (M15)
// ============================================================
struct FreeRegion { u64 va; u32 pages; };
constexpr usize FREE_LIST_SLOTS = 16;

// ============================================================
// KProcess forward-declared so KThread can reference it
// ============================================================
struct KProcess;

// ============================================================
// KThread -- Kernel Thread Control Block
//
// CRITICAL: KernelStackPtr MUST stay at offset 0.
// switch_context (switch.asm) reads/writes [rdi+0] / [rsi+0].
// ============================================================
struct KThread {
    u64         KernelStackPtr;     // offset 0: saved kernel RSP

    u32         Tid;
    ThreadState State;
    u32         QuantumLeft;
    u32         Flags;
    u32         Priority;           // M15: THREAD_PRIORITY_*

    KProcess*   Process;

    u64         KernelStackBase;
    usize       KernelStackSize;

    void*       EntryFn;
    void*       EntryArg;

    char        Name[32];

    // Ready-queue links (doubly-linked via sentinel)
    KThread*    Next;
    KThread*    Prev;

    // Event / semaphore / mutant wait queue (singly-linked)
    KThread*    WaitNext;

    // Sleep support  (M13)
    u64         SleepUntil;
    KThread*    SleepNext;

    // User-mode argument passed via extended IRETQ frame  (M13)
    u64         UserArg;

    // M15: exception handler VA (0 = none)
    u64         ExceptionHandler;

    // Windows compat: per-thread TEB virtual address (0 = none/kernel thread).
    // Loaded into the user GS base when this thread is scheduled.
    u64         TebVa;
};

// ============================================================
// KProcess -- Kernel Process Control Block
// ============================================================
struct KProcess {
    u32         Pid;
    u32         Flags;
    u64         Cr3;                // PML4 physical address
    u64         UserHeapCursor;     // bump allocator base (init 0x500000000)
    char        Name[32];
    i32         ExitStatus;

    // M15: per-process free list
    FreeRegion  UserFreeList[FREE_LIST_SLOTS];

    // M17: process lifetime tracking
    u32         thread_count;
    bool        exited;
    KThread*    exit_waiters;       // threads waiting for this process to exit

    // Windows compat: PEB virtual address (0 = none) and the next TEB VA to
    // hand out for threads of this process.
    u64         PebVa;
    u64         NextTebVa;
};

// ============================================================
// PS namespace -- process/thread creation
// ============================================================
namespace PS {

constexpr u32 QUANTUM_TICKS = 5;   // exposed so scheduler can use it

void      Init();

KProcess* CreateProcess(const char* name, u64 cr3 = 0);
void      DestroyProcess(KProcess* process);

KThread*  CreateKernelThread(KProcess* process, const char* name,
                               void (*entry_fn)(void*), void* arg,
                               usize kernel_stack_size = 16384);

KThread*  CreateUserThread(KProcess* process, const char* name,
                             u64 user_entry_va, u64 user_stack_va,
                             u64 user_arg = 0,
                             usize kernel_stack_size = 16384);

[[noreturn]] void TerminateCurrentThread(i32 exit_code = 0);

KProcess* SystemProcess();
KThread*  MainThread();
u32       ProcessCount();    // M19: number of registered processes
KProcess* GetProcess(u32 i); // M19: get process by registry index
bool      KillProcess(u32 pid); // M32: terminate process and all its threads
u32       ThreadCount();     // M32: number of registered threads
KThread*  GetThread(u32 i);  // M32: get thread by registry index

} // namespace PS

// ============================================================
// Sched namespace -- priority scheduler  (M15: 3-level queues)
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
// Assembly trampolines (ps/switch.asm)
// ============================================================
extern "C" {
    void switch_context(KThread* prev, KThread* next);
    void kernel_thread_entry();
    void user_thread_entry();
}
