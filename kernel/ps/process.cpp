// process.cpp - MicroNT Process/Thread Manager (M1: stub)

#include "../include/process.h"
#include "../include/memory.h"
#include "../include/debug.h"

namespace PS {

namespace {
    static KProcess* s_system_process = nullptr;
    static u32       s_next_pid = 1;
    static u32       s_next_tid = 1;
}

void Init() {
    // Create system process stub
    s_system_process = static_cast<KProcess*>(
        KernelHeap::AllocZeroed(sizeof(KProcess)));
    if (!s_system_process) {
        KDBG_ERROR("PS: failed to allocate system process");
        return;
    }
    s_system_process->Pid = s_next_pid++;
    constexpr char sysname[] = "System";
    for (int i = 0; sysname[i]; ++i)
        s_system_process->Name[i] = sysname[i];

    // TODO(M5): Scheduler, thread creation, ring-3 transition
    KDBG_INFO("PS: Process manager initialized (M1 stub)");
}

KProcess* CreateProcess(const char* name) {
    // TODO(M5): Full process creation with address space
    auto* p = static_cast<KProcess*>(KernelHeap::AllocZeroed(sizeof(KProcess)));
    if (!p) return nullptr;
    p->Pid = s_next_pid++;
    for (int i = 0; name && name[i] && i < 63; ++i)
        p->Name[i] = name[i];
    KDBG_INFO("PS: CreateProcess '%s' pid=%u", p->Name, p->Pid);
    return p;
}

KThread* CreateThread(KProcess* process, u64 entry_point, u64 stack_top) {
    // TODO(M5): Full thread creation with context switch support
    auto* t = static_cast<KThread*>(KernelHeap::AllocZeroed(sizeof(KThread)));
    if (!t) return nullptr;
    t->Process    = process;
    t->EntryPoint = entry_point;
    t->Rsp        = stack_top;
    t->Tid        = s_next_tid++;
    t->State      = ThreadState::Initialized;
    KDBG_INFO("PS: CreateThread tid=%u entry=0x%llx", t->Tid, entry_point);
    return t;
}

KProcess* CurrentProcess() { return s_system_process; }
KThread*  CurrentThread()  { return nullptr; } // TODO(M5)

void TerminateProcess(KProcess* process, NTSTATUS exit_status) {
    if (!process) return;
    process->ExitStatus   = exit_status;
    process->IsTerminated = true;
    KDBG_INFO("PS: Process pid=%u terminated with 0x%x",
        process->Pid, exit_status);
}

void TerminateThread(KThread* thread, NTSTATUS exit_status) {
    if (!thread) return;
    thread->ExitStatus = exit_status;
    thread->State      = ThreadState::Terminated;
}

} // namespace PS
