// process.cpp - MicroNT M5 Process and Thread creation

#include "../include/process.h"
#include "../include/memory.h"
#include "../include/debug.h"
#include "../include/hal.h"

// ============================================================
// Stack frame sizes (in bytes)
// switch_context saves: rbp rbx r12 r13 r14 r15 + implicit ret addr = 7 * 8
// ============================================================
constexpr usize SWITCH_FRAME_SIZE = 7 * 8;  // 56 bytes

// For user threads, IRETQ frame sits above the switch frame:
// [RIP, CS, RFLAGS, RSP_user, SS] = 5 * 8 = 40 bytes
constexpr usize IRETQ_FRAME_SIZE  = 5 * 8;  // 40 bytes

// User-mode segment selectors
constexpr u64 USER_CS = 0x1B;   // index 3, GDT, DPL=3
constexpr u64 USER_SS = 0x23;   // index 4, GDT, DPL=3
constexpr u64 USER_FLAGS = 0x202; // IF=1, reserved bit 1

namespace {

static KProcess* s_system_process = nullptr;
static KThread*  s_main_thread    = nullptr;

static u32 s_next_pid = 0;
static u32 s_next_tid = 0;

// Allocate contiguous physical pages for a kernel stack.
// Returns physical base (= virtual base via identity map), or 0.
static u64 AllocStack(usize size) {
    // Allocate page by page (PMM::AllocPage returns single pages).
    // For simplicity, allocate contiguous pages manually.
    usize pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    // Allocate first page
    u64 base = PMM::AllocPage();
    if (!base) return 0;
    // Allocate remaining pages (hope they're contiguous - bump PMM usually is)
    for (usize i = 1; i < pages; ++i) {
        u64 p = PMM::AllocPage();
        if (!p) return 0;  // leak, but this is M5
    }
    return base;
}

// Write a u64 to the stack slot (1-indexed from top).
// slot=1: [top - 8], slot=2: [top - 16], ...
static void StackPoke(u64 top, u32 slot, u64 value) {
    auto* p = reinterpret_cast<u64*>(top - (u64)slot * 8);
    *p = value;
}

} // anonymous namespace

// ============================================================
// C++ entry for kernel threads (called from kernel_thread_entry asm)
// ============================================================
extern "C" void KernelThreadEntry() {
    KThread* t = Sched::CurrentThread();
    if (t && t->EntryFn) {
        using Fn = void(*)(void*);
        reinterpret_cast<Fn>(t->EntryFn)(t->EntryArg);
    }
    PS::TerminateCurrentThread(0);
}

namespace PS {

void Init() {
    // ----------------------------------------------------------
    // 1. Create System process (PID 0) using the current CR3
    // ----------------------------------------------------------
    s_system_process = static_cast<KProcess*>(
        KernelHeap::AllocZeroed(sizeof(KProcess), alignof(KProcess)));
    KASSERT(s_system_process);
    s_system_process->Pid   = s_next_pid++;
    s_system_process->Cr3   = HAL::ReadCr3() & 0x000FFFFFFFFFF000ULL;
    s_system_process->Flags = 0;
    // Use str_copy equivalent via a simple loop
    const char* sn = "System";
    for (int i = 0; i < 31 && sn[i]; ++i) s_system_process->Name[i] = sn[i];
    KDBG_INFO("PS: System process PID=%u CR3=0x%llx", s_system_process->Pid, s_system_process->Cr3);

    // ----------------------------------------------------------
    // 2. Create main thread (represents the current kernel_main execution)
    // KernelStackPtr is populated on first switch_context call.
    // ----------------------------------------------------------
    s_main_thread = static_cast<KThread*>(
        KernelHeap::AllocZeroed(sizeof(KThread), alignof(KThread)));
    KASSERT(s_main_thread);
    s_main_thread->Tid            = s_next_tid++;
    s_main_thread->State          = ThreadState::RUNNING;
    s_main_thread->QuantumLeft    = Sched::QUANTUM_TICKS;
    s_main_thread->Process        = s_system_process;
    s_main_thread->KernelStackPtr = 0;  // set on first switch_context save
    const char* mn = "Main";
    for (int i = 0; i < 31 && mn[i]; ++i) s_main_thread->Name[i] = mn[i];

    // ----------------------------------------------------------
    // 3. Initialize scheduler and register main thread as current
    // ----------------------------------------------------------
    Sched::Init();
    KDBG_INFO("PS: initialized (PID=%u TID=%u)", s_system_process->Pid, s_main_thread->Tid);
}

KProcess* CreateProcess(const char* name, u64 cr3) {
    auto* p = static_cast<KProcess*>(
        KernelHeap::AllocZeroed(sizeof(KProcess), alignof(KProcess)));
    if (!p) return nullptr;
    p->Pid   = s_next_pid++;
    p->Flags = 0;
    p->Cr3   = cr3 ? cr3 : (HAL::ReadCr3() & 0x000FFFFFFFFFF000ULL);
    if (name) for (int i = 0; i < 31 && name[i]; ++i) p->Name[i] = name[i];
    return p;
}

void DestroyProcess(KProcess* /*process*/) {
    // TODO(M6): reclaim address space, handle table, etc.
}

KThread* CreateKernelThread(KProcess* process, const char* name,
                             void (*entry_fn)(void*), void* arg,
                             usize kernel_stack_size) {
    // Allocate kernel stack
    u64 stack_base = AllocStack(kernel_stack_size);
    if (!stack_base) { KDBG_ERROR("PS: CreateKernelThread: out of memory for stack"); return nullptr; }

    u64 stack_top = stack_base + kernel_stack_size;  // exclusive top (identity-mapped)

    // Build switch_context initial frame at the top of the stack.
    // Layout (7 slots from top):
    //   slot 1 (top-8 ): return addr = kernel_thread_entry
    //   slot 2 (top-16): rbp = 0
    //   slot 3 (top-24): rbx = 0
    //   slot 4 (top-32): r12 = 0
    //   slot 5 (top-40): r13 = 0
    //   slot 6 (top-48): r14 = 0
    //   slot 7 (top-56): r15 = 0
    //   KernelStackPtr = top - 56
    StackPoke(stack_top, 1, reinterpret_cast<u64>(kernel_thread_entry));
    StackPoke(stack_top, 2, 0);   // rbp
    StackPoke(stack_top, 3, 0);   // rbx
    StackPoke(stack_top, 4, 0);   // r12
    StackPoke(stack_top, 5, 0);   // r13
    StackPoke(stack_top, 6, 0);   // r14
    StackPoke(stack_top, 7, 0);   // r15

    // Allocate KThread
    auto* t = static_cast<KThread*>(
        KernelHeap::AllocZeroed(sizeof(KThread), alignof(KThread)));
    if (!t) return nullptr;

    t->Tid             = s_next_tid++;
    t->State           = ThreadState::READY;
    t->QuantumLeft     = Sched::QUANTUM_TICKS;
    t->Process         = process ? process : s_system_process;
    t->KernelStackBase = stack_base;
    t->KernelStackSize = kernel_stack_size;
    t->EntryFn         = reinterpret_cast<void*>(entry_fn);
    t->EntryArg        = arg;
    t->KernelStackPtr  = stack_top - SWITCH_FRAME_SIZE;
    t->Next            = nullptr;
    t->Prev            = nullptr;
    if (name) for (int i = 0; i < 31 && name[i]; ++i) t->Name[i] = name[i];

    KDBG_TRACE("PS: CreateKernelThread '%s' TID=%u stack=0x%llx..0x%llx KSP=0x%llx",
               t->Name, t->Tid, stack_base, stack_top, t->KernelStackPtr);
    return t;
}

KThread* CreateUserThread(KProcess* process, const char* name,
                           u64 user_entry_va, u64 user_stack_va,
                           usize kernel_stack_size) {
    u64 stack_base = AllocStack(kernel_stack_size);
    if (!stack_base) return nullptr;

    u64 stack_top = stack_base + kernel_stack_size;

    // Layout from top of kernel stack:
    // IRETQ frame (5 slots): SS, RSP_user, RFLAGS, CS, RIP
    //   slot 1 (top-8 ): SS = USER_SS
    //   slot 2 (top-16): RSP_user = user_stack_va
    //   slot 3 (top-24): RFLAGS = USER_FLAGS
    //   slot 4 (top-32): CS = USER_CS
    //   slot 5 (top-40): RIP = user_entry_va
    // switch_context frame (7 slots below IRETQ frame):
    //   slot 6 (top-48): return addr = user_thread_entry
    //   slot 7 (top-56): rbp = 0
    //   slot 8 (top-64): rbx = 0
    //   slot 9 (top-72): r12 = 0
    //   slot 10(top-80): r13 = 0
    //   slot 11(top-88): r14 = 0
    //   slot 12(top-96): r15 = 0
    //   KernelStackPtr = top - 96
    StackPoke(stack_top,  1, USER_SS);                                      // SS
    StackPoke(stack_top,  2, user_stack_va);                                // RSP_user
    StackPoke(stack_top,  3, USER_FLAGS);                                   // RFLAGS
    StackPoke(stack_top,  4, USER_CS);                                      // CS
    StackPoke(stack_top,  5, user_entry_va);                                // RIP
    StackPoke(stack_top,  6, reinterpret_cast<u64>(user_thread_entry));    // ret addr
    StackPoke(stack_top,  7, 0);   // rbp
    StackPoke(stack_top,  8, 0);   // rbx
    StackPoke(stack_top,  9, 0);   // r12
    StackPoke(stack_top, 10, 0);   // r13
    StackPoke(stack_top, 11, 0);   // r14
    StackPoke(stack_top, 12, 0);   // r15

    auto* t = static_cast<KThread*>(
        KernelHeap::AllocZeroed(sizeof(KThread), alignof(KThread)));
    if (!t) return nullptr;

    t->Tid             = s_next_tid++;
    t->State           = ThreadState::READY;
    t->QuantumLeft     = Sched::QUANTUM_TICKS;
    t->Process         = process ? process : s_system_process;
    t->KernelStackBase = stack_base;
    t->KernelStackSize = kernel_stack_size;
    t->KernelStackPtr  = stack_top - SWITCH_FRAME_SIZE - IRETQ_FRAME_SIZE;
    t->EntryFn         = nullptr;  // entry is via IRETQ, not EntryFn
    t->EntryArg        = nullptr;
    t->Next            = nullptr;
    t->Prev            = nullptr;
    if (name) for (int i = 0; i < 31 && name[i]; ++i) t->Name[i] = name[i];

    KDBG_TRACE("PS: CreateUserThread '%s' TID=%u entry=0x%llx user_rsp=0x%llx",
               t->Name, t->Tid, user_entry_va, user_stack_va);
    return t;
}

[[noreturn]] void TerminateCurrentThread(i32 exit_code) {
    KThread* t = Sched::CurrentThread();
    if (t) {
        KDBG_TRACE("PS: thread '%s' TID=%u terminated (exit=%d)",
                   t->Name, t->Tid, exit_code);
        t->State = ThreadState::TERMINATED;
    }
    // Force a context switch; Schedule() will not re-add this thread
    // because its State is TERMINATED.  Never returns.
    Sched::Schedule();
    // If somehow Schedule returns (e.g., no other threads), halt.
    HAL::CpuHalt();
}

KProcess* SystemProcess() { return s_system_process; }
KThread*  MainThread()    { return s_main_thread; }

} // namespace PS
