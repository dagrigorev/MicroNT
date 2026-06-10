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

// User-mode segment selectors (must match GDT layout in gdt.cpp)
// GDT[3]=user_data -> selector 0x18|3=0x1B
// GDT[4]=user_code -> selector 0x20|3=0x23
// STAR[63:48]=0x10: SYSRET  CS=0x10+16|3=0x23  SS=0x10+8|3=0x1B
constexpr u64 USER_CS    = 0x23;   // GDT[4] | DPL=3
constexpr u64 USER_SS    = 0x1B;   // GDT[3] | DPL=3
constexpr u64 USER_FLAGS = 0x202;  // IF=1, reserved bit 1

namespace {

static KProcess* s_system_process = nullptr;
static KThread*  s_main_thread    = nullptr;

// M19: global process registry
static KProcess* s_proc_reg[32] = {};
static u32       s_proc_count   = 0;

// M32: global thread registry (for kill-by-PID)
static KThread*  s_thread_reg[64] = {};
static u32       s_thread_count   = 0;

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

// ============================================================
// Windows process environment: PEB / TEB
//
// VAs sit at ~8 TB, far above the 0-4 GB identity huge-page region and any
// existing user mappings, so they map cleanly with 4 KB pages. Field offsets
// follow the documented x64 PEB/TEB layout.
// ============================================================
constexpr u64 PEB_VA   = 0x7FFFE000000ULL;
constexpr u64 TEB_BASE = 0x7FFFE100000ULL;

// Reported OS identity (Windows 11 24H2) -- matches KUSER_SHARED_DATA.
constexpr u32 OS_MAJOR = 10, OS_MINOR = 0, OS_BUILD = 26100;

// PEB->Ldr area, one page above the PEB.
constexpr u64 LDR_VA          = PEB_VA + 0x1000;   // PEB_LDR_DATA
constexpr u64 LDR_ENTRY_OFF   = 0x80;              // LDR_DATA_TABLE_ENTRY
constexpr u64 LDR_BASENAME_OFF= 0x140;
constexpr u64 LDR_FULLNAME_OFF= 0x200;

static void SetupPeb(KProcess* p) {
    if (!p || !p->Cr3) return;
    u64 phys = PMM::AllocPage();
    if (!phys) return;
    auto* peb = reinterpret_cast<u8*>(phys);   // identity-mapped (< RAM)
    for (u32 i = 0; i < PAGE_SIZE; ++i) peb[i] = 0;
    *reinterpret_cast<u64*>(peb + 0x010) = p->ImageBase;  // ImageBaseAddress
    *reinterpret_cast<u64*>(peb + 0x018) = LDR_VA;        // Ldr (PEB_LDR_DATA*)
    *reinterpret_cast<u32*>(peb + 0x118) = OS_MAJOR;      // OSMajorVersion
    *reinterpret_cast<u32*>(peb + 0x11C) = OS_MINOR;      // OSMinorVersion
    *reinterpret_cast<u16*>(peb + 0x120) = (u16)OS_BUILD; // OSBuildNumber
    *reinterpret_cast<u32*>(peb + 0x124) = 2;             // OSPlatformId = VER_PLATFORM_WIN32_NT
    p->PebVa = PEB_VA;
    p->NextTebVa = TEB_BASE;
    VMM::MapPageInto(p->Cr3, PEB_VA, phys,
                     VMM::PTE_PRESENT | VMM::PTE_WRITABLE | VMM::PTE_USER);

    // PEB->Ldr: a walkable loaded-module list with one entry for the main
    // image (three circular LIST_ENTRY chains, exactly as Windows builds it).
    u64 lphys = PMM::AllocPage();
    if (!lphys) return;
    auto* L = reinterpret_cast<u8*>(lphys);
    for (u32 i = 0; i < PAGE_SIZE; ++i) L[i] = 0;
    auto wq = [&](u64 off, u64 v) { *reinterpret_cast<u64*>(L + off) = v; };
    auto wd = [&](u64 off, u32 v) { *reinterpret_cast<u32*>(L + off) = v; };
    auto ww = [&](u64 off, u16 v) { *reinterpret_cast<u16*>(L + off) = v; };

    const u64 E = LDR_ENTRY_OFF;
    const u64 entry_va = LDR_VA + E;
    // PEB_LDR_DATA
    wd(0x00, 0x58); wd(0x04, 1);                          // Length, Initialized
    wq(0x10, entry_va + 0x00); wq(0x18, entry_va + 0x00); // InLoadOrderModuleList
    wq(0x20, entry_va + 0x10); wq(0x28, entry_va + 0x10); // InMemoryOrderModuleList
    wq(0x30, entry_va + 0x20); wq(0x38, entry_va + 0x20); // InInitializationOrderModuleList
    // LDR_DATA_TABLE_ENTRY (links point back to the three list heads)
    wq(E + 0x00, LDR_VA + 0x10); wq(E + 0x08, LDR_VA + 0x10);
    wq(E + 0x10, LDR_VA + 0x20); wq(E + 0x18, LDR_VA + 0x20);
    wq(E + 0x20, LDR_VA + 0x30); wq(E + 0x28, LDR_VA + 0x30);
    wq(E + 0x30, p->ImageBase);                           // DllBase
    wq(E + 0x38, 0);                                      // EntryPoint
    wd(E + 0x40, 0);                                      // SizeOfImage
    u32 nlen = 0; while (p->Name[nlen]) ++nlen;
    ww(E + 0x48, (u16)(nlen * 2)); ww(E + 0x4A, (u16)(nlen * 2 + 2));
    wq(E + 0x50, LDR_VA + LDR_FULLNAME_OFF);              // FullDllName.Buffer
    ww(E + 0x58, (u16)(nlen * 2)); ww(E + 0x5A, (u16)(nlen * 2 + 2));
    wq(E + 0x60, LDR_VA + LDR_BASENAME_OFF);              // BaseDllName.Buffer
    auto* bn = reinterpret_cast<u16*>(L + LDR_BASENAME_OFF);
    auto* fn = reinterpret_cast<u16*>(L + LDR_FULLNAME_OFF);
    for (u32 i = 0; i < nlen; ++i) { bn[i] = (u16)(u8)p->Name[i]; fn[i] = (u16)(u8)p->Name[i]; }
    VMM::MapPageInto(p->Cr3, LDR_VA, lphys,
                     VMM::PTE_PRESENT | VMM::PTE_WRITABLE | VMM::PTE_USER);

    // PEB->ProcessParameters: RTL_USER_PROCESS_PARAMETERS with ImagePathName
    // and CommandLine, as read by GetCommandLineW / the CRT startup.
    u64 pphys = PMM::AllocPage();
    if (!pphys) return;
    auto* P = reinterpret_cast<u8*>(pphys);
    for (u32 i = 0; i < PAGE_SIZE; ++i) P[i] = 0;
    constexpr u64 PARAMS_VA = PEB_VA + 0x2000;
    constexpr u64 IMG_OFF = 0x100, CMD_OFF = 0x200;
    *reinterpret_cast<u32*>(P + 0x00) = PAGE_SIZE;   // MaximumLength
    *reinterpret_cast<u32*>(P + 0x04) = PAGE_SIZE;   // Length
    // ImagePathName = "C:\Windows\<name>"
    char path[96]; u32 pl = 0;
    const char* pre = "C:\\Windows\\";
    for (u32 i = 0; pre[i]; ++i) path[pl++] = pre[i];
    for (u32 i = 0; p->Name[i] && pl < 94; ++i) path[pl++] = p->Name[i];
    path[pl] = 0;
    auto* iw = reinterpret_cast<u16*>(P + IMG_OFF);
    for (u32 i = 0; i < pl; ++i) iw[i] = (u16)(u8)path[i];
    *reinterpret_cast<u16*>(P + 0x60) = (u16)(pl * 2);       // ImagePathName.Length
    *reinterpret_cast<u16*>(P + 0x62) = (u16)(pl * 2 + 2);
    *reinterpret_cast<u64*>(P + 0x68) = PARAMS_VA + IMG_OFF; // .Buffer
    // CommandLine = "<name>"
    auto* cw = reinterpret_cast<u16*>(P + CMD_OFF);
    for (u32 i = 0; i < nlen; ++i) cw[i] = (u16)(u8)p->Name[i];
    *reinterpret_cast<u16*>(P + 0x70) = (u16)(nlen * 2);     // CommandLine.Length
    *reinterpret_cast<u16*>(P + 0x72) = (u16)(nlen * 2 + 2);
    *reinterpret_cast<u64*>(P + 0x78) = PARAMS_VA + CMD_OFF; // .Buffer
    *reinterpret_cast<u64*>(peb + 0x20) = PARAMS_VA;         // PEB.ProcessParameters
    VMM::MapPageInto(p->Cr3, PARAMS_VA, pphys,
                     VMM::PTE_PRESENT | VMM::PTE_WRITABLE | VMM::PTE_USER);

    // PEB.ProcessHeap: a committed region GetProcessHeap()/HeapAlloc build on.
    u64 hphys = PMM::AllocPage();
    if (hphys) {
        auto* H = reinterpret_cast<u8*>(hphys);
        for (u32 i = 0; i < PAGE_SIZE; ++i) H[i] = 0;
        constexpr u64 HEAP_VA = PEB_VA + 0x3000;
        *reinterpret_cast<u64*>(peb + 0x30) = HEAP_VA;        // PEB.ProcessHeap
        VMM::MapPageInto(p->Cr3, HEAP_VA, hphys,
                         VMM::PTE_PRESENT | VMM::PTE_WRITABLE | VMM::PTE_USER);
    }
}

static u64 SetupTeb(KProcess* p, u32 tid, u64 user_stack_top) {
    if (!p || !p->Cr3 || !p->PebVa) return 0;
    u64 teb_va = p->NextTebVa;
    p->NextTebVa += 0x2000;   // one page + guard slot
    u64 phys = PMM::AllocPage();
    if (!phys) return 0;
    auto* teb = reinterpret_cast<u8*>(phys);
    for (u32 i = 0; i < PAGE_SIZE; ++i) teb[i] = 0;
    *reinterpret_cast<u64*>(teb + 0x08) = user_stack_top;   // NT_TIB.StackBase
    *reinterpret_cast<u64*>(teb + 0x30) = teb_va;           // NT_TIB.Self
    *reinterpret_cast<u64*>(teb + 0x40) = p->Pid;           // ClientId.UniqueProcess
    *reinterpret_cast<u64*>(teb + 0x48) = tid;              // ClientId.UniqueThread
    *reinterpret_cast<u64*>(teb + 0x60) = p->PebVa;         // ProcessEnvironmentBlock
    // ThreadLocalStoragePointer -> a zeroed 64-slot TLS array inside this page
    // (TEB is otherwise unused past 0x68; 0x800 is comfortably clear).
    *reinterpret_cast<u64*>(teb + 0x58) = teb_va + 0x800;
    VMM::MapPageInto(p->Cr3, teb_va, phys,
                     VMM::PTE_PRESENT | VMM::PTE_WRITABLE | VMM::PTE_USER);
    return teb_va;
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
    s_system_process->thread_count = 1; // main kernel thread keeps System alive
    s_main_thread->KernelStackPtr = 0;  // set on first switch_context save
    const char* mn = "Main";
    for (int i = 0; i < 31 && mn[i]; ++i) s_main_thread->Name[i] = mn[i];

    // ----------------------------------------------------------
    // 3. Initialize scheduler and register main thread as current
    // ----------------------------------------------------------
    Sched::Init();
    if (s_proc_count < 32) s_proc_reg[s_proc_count++] = s_system_process; // M19
    KDBG_INFO("PS: initialized (PID=%u TID=%u)", s_system_process->Pid, s_main_thread->Tid);
}

void NotifyImageLoaded(u64 cr3, u64 image_base) {
    for (u32 i = 0; i < s_proc_count; ++i) {
        KProcess* p = s_proc_reg[i];
        if (!p || p->Cr3 != cr3 || !p->PebVa) continue;
        p->ImageBase = image_base;
        // Patch the live structures in the process address space.
        u64 ph = VMM::TranslateInPml4(cr3, PEB_VA + 0x10);
        if (ph) *reinterpret_cast<u64*>(ph) = image_base;     // PEB.ImageBaseAddress
        u64 lh = VMM::TranslateInPml4(cr3, LDR_VA + LDR_ENTRY_OFF + 0x30);
        if (lh) *reinterpret_cast<u64*>(lh) = image_base;     // Ldr entry DllBase
        return;
    }
}

KProcess* CreateProcess(const char* name, u64 cr3) {
    auto* p = static_cast<KProcess*>(
        KernelHeap::AllocZeroed(sizeof(KProcess), alignof(KProcess)));
    if (!p) return nullptr;
    p->Pid            = s_next_pid++;
    p->Flags          = 0;
    p->Cr3            = cr3 ? cr3 : (HAL::ReadCr3() & 0x000FFFFFFFFFF000ULL);
    p->UserHeapCursor = 0x500000000ULL;  // each process starts its own heap here
    if (name) for (int i = 0; i < 31 && name[i]; ++i) p->Name[i] = name[i];
    if (s_proc_count < 32) s_proc_reg[s_proc_count++] = p; // M19 registry
    SetupPeb(p);   // Windows compat: per-process PEB at 0x7FFFE000000
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
    t->Priority        = THREAD_PRIORITY_NORMAL;
    t->Process         = process ? process : s_system_process;
    if (t->Process) t->Process->thread_count++;
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
                           u64 user_arg,
                           usize kernel_stack_size) {
    u64 stack_base = AllocStack(kernel_stack_size);
    if (!stack_base) return nullptr;

    u64 stack_top = stack_base + kernel_stack_size;

    // Stack layout (top = lowest address, built top-down):
    //   IRETQ frame (5 slots): SS, RSP_user, RFLAGS, CS, RIP
    //   user_arg slot          (1 slot)    : popped as RDI by user_thread_entry
    //   switch_context frame   (7 slots): r15=0..rbp=0, return=user_thread_entry
    //   KernelStackPtr = stack_top - 13*8
    StackPoke(stack_top,  1, USER_SS);
    StackPoke(stack_top,  2, user_stack_va);
    StackPoke(stack_top,  3, USER_FLAGS);
    StackPoke(stack_top,  4, USER_CS);
    StackPoke(stack_top,  5, user_entry_va);
    StackPoke(stack_top,  6, user_arg);                                     // popped as RDI
    StackPoke(stack_top,  7, reinterpret_cast<u64>(user_thread_entry));    // ret addr
    StackPoke(stack_top,  8, 0);   // rbp
    StackPoke(stack_top,  9, 0);   // rbx
    StackPoke(stack_top, 10, 0);   // r12
    StackPoke(stack_top, 11, 0);   // r13
    StackPoke(stack_top, 12, 0);   // r14
    StackPoke(stack_top, 13, 0);   // r15

    auto* t = static_cast<KThread*>(
        KernelHeap::AllocZeroed(sizeof(KThread), alignof(KThread)));
    if (!t) return nullptr;

    t->Tid             = s_next_tid++;
    t->State           = ThreadState::READY;
    t->QuantumLeft     = Sched::QUANTUM_TICKS;
    t->Priority        = THREAD_PRIORITY_NORMAL;
    t->Process         = process ? process : s_system_process;
    if (t->Process) t->Process->thread_count++;
    t->KernelStackBase = stack_base;
    t->KernelStackSize = kernel_stack_size;
    t->KernelStackPtr  = stack_top - SWITCH_FRAME_SIZE - IRETQ_FRAME_SIZE - 8; // -8 for user_arg slot
    t->EntryFn         = nullptr;  // entry is via IRETQ, not EntryFn
    t->EntryArg        = nullptr;
    t->Next            = nullptr;
    t->Prev            = nullptr;
    if (name) for (int i = 0; i < 31 && name[i]; ++i) t->Name[i] = name[i];

    // Windows compat: give the thread a TEB (GS base) linked to the PEB.
    t->TebVa = SetupTeb(t->Process, t->Tid, user_stack_va);

    KDBG_TRACE("PS: CreateUserThread '%s' TID=%u entry=0x%llx user_rsp=0x%llx teb=0x%llx",
               t->Name, t->Tid, user_entry_va, user_stack_va, t->TebVa);
    // M32: register in global thread table for kill-by-PID
    if (s_thread_count < 64) s_thread_reg[s_thread_count++] = t;
    return t;
}

[[noreturn]] void TerminateCurrentThread(i32 exit_code) {
    KThread* t = Sched::CurrentThread();
    if (t) {
        KDBG_TRACE("PS: thread '%s' TID=%u terminated (exit=%d)",
                   t->Name, t->Tid, exit_code);
        t->State = ThreadState::TERMINATED;
    // M17: notify process exit when last thread terminates
    KProcess* tp = t->Process;
    if (tp && tp->thread_count > 0) {
        if (--tp->thread_count == 0) {
            tp->exited = true;
            KThread* w = tp->exit_waiters;
            tp->exit_waiters = nullptr;
            while (w) {
                KThread* nxt = w->WaitNext;
                w->WaitNext = nullptr;
                Sched::UnblockThread(w);
                w = nxt;
            }
        }
    }
    }
    // Force a context switch; Schedule() will not re-add this thread
    // because its State is TERMINATED.  Never returns.
    Sched::Schedule();
    // If somehow Schedule returns (e.g., no other threads), halt.
    HAL::CpuHalt();
}

KProcess* SystemProcess() { return s_system_process; }
KThread*  MainThread()    { return s_main_thread; }


u32 ProcessCount() { return s_proc_count; }
KProcess* GetProcess(u32 i) { return i < s_proc_count ? s_proc_reg[i] : nullptr; }

// M32: terminate all threads belonging to process with given PID.
// Marks process as exited and all its threads as TERMINATED.
bool KillProcess(u32 pid) {
    // Find the process
    KProcess* target = nullptr;
    for (u32 i = 0; i < s_proc_count; ++i) {
        if (s_proc_reg[i] && s_proc_reg[i]->Pid == pid &&
            !s_proc_reg[i]->exited) {
            target = s_proc_reg[i];
            break;
        }
    }
    if (!target) return false;

    // Mark all its threads as terminated
    for (u32 i = 0; i < s_thread_count; ++i) {
        KThread* t = s_thread_reg[i];
        if (t && t->Process == target &&
            t->State != ThreadState::TERMINATED) {
            t->State = ThreadState::TERMINATED;
        }
    }
    // Mark process as exited and wake any waiters
    target->thread_count = 0;
    target->exited       = true;
    target->ExitStatus   = -1;  // killed
    KThread* w = target->exit_waiters;
    target->exit_waiters = nullptr;
    while (w) {
        KThread* nxt = w->WaitNext;
        w->WaitNext = nullptr;
        Sched::UnblockThread(w);
        w = nxt;
    }
    KDBG_INFO("PS: KillProcess PID=%u ('%s')", pid, target->Name);
    return true;
}

u32 ThreadCount() { return s_thread_count; }
KThread* GetThread(u32 i) { return i < s_thread_count ? s_thread_reg[i] : nullptr; }

} // namespace PS
