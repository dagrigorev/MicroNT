#pragma once
// process.h - MicroNT M5: Process and Thread management

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
};

// ============================================================
// KProcess - Kernel Process Control Block
// ============================================================
struct KProcess {
    u32         Pid;
    u32         Flags;
    u64         Cr3;              // physical address of PML4
    char        Name[32];
    i32         ExitStatus;
};

// ============================================================
// PS - Process/Thread creation
// ============================================================
namespace PS {

void     Init();

KProcess* CreateProcess(const char* name, u64 cr3 = 0);
void      DestroyProcess(KProcess* process);

// Allocate and set up a kernel thread (runs entry_fn(arg) in ring 0).
// Does NOT add to the scheduler ready queue - caller does Sched::AddThread().
KThread* CreateKernelThread(KProcess* process, const char* name,
                             void (*entry_fn)(void*), void* arg,
                             usize kernel_stack_size = 16384);

// Allocate and set up a user thread (transitions to ring 3 via IRETQ).
// user_stack_va: top of user stack (already mapped by caller).
// user_arg: value passed to the thread in RDI (via extended IRETQ frame).
KThread* CreateUserThread(KProcess* process, const char* name,
                           u64 user_entry_va, u64 user_stack_va,
                           u64 user_arg = 0,
                           usize kernel_stack_size = 16384);

// Mark current thread TERMINATED and yield. Does not return.
[[noreturn]] void TerminateCurrentThread(i32 exit_code = 0);

KProcess* SystemProcess();
KThread*  MainThread();    // the "boot" thread representing kernel_main

} // namespace PS

// ============================================================
// Sched - Round-robin scheduler
// ============================================================
namespace Sched {

constexpr u32 QUANTUM_TICKS = 5;  // 5 ticks = 50 ms at 100 Hz

void     Init();
void     Start();             // enable preemption; boot thread becomes main thread
void     AddThread(KThread* t);
void     RemoveThread(KThread* t);  // remove from ready queue (if present)
void     Tick();              // called from IRQ0 handler (interrupts disabled)
void     Schedule();          // cooperative yield (interrupts must be enabled)
KThread* CurrentThread();
bool     IsActive();

// Block the current thread (remove from ready queue, mark BLOCKED).
// Caller must call Schedule() after this to switch to another thread.
// Must be called with interrupts DISABLED.
void     BlockCurrentThread();

// Unblock a thread (mark READY and add to ready queue).
// Safe to call with interrupts enabled or disabled.
void     UnblockThread(KThread* t);

// Put current thread to sleep for 'ms' milliseconds (uses PIT ticks).
void     Sleep(u32 ms);

} // namespace Sched

// ============================================================
// Assembly trampolines (switch.asm)
// ============================================================
extern "C" {
    void switch_context(KThread* prev, KThread* next);  // save/restore RSP + callee-saved
    void kernel_thread_entry();   // entry trampoline for kernel threads
    void user_thread_entry();     // IRETQ trampoline for user threads
}
