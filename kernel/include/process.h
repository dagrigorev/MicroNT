#pragma once
// process.h - MicroNT Process Manager interface

#include "ntdef.h"
#include "ntstatus.h"

// ============================================================
// Thread states
// ============================================================
enum class ThreadState : u32 {
    Initialized = 0,
    Ready       = 1,
    Running     = 2,
    Waiting     = 3,
    Terminated  = 4,
};

// ============================================================
// Forward declarations
// ============================================================
struct KProcess;
struct KThread;

// ============================================================
// Process Manager
// ============================================================
namespace PS {

void Init();

// Create a minimal system process (M1: stub)
KProcess* CreateProcess(const char* name);

// Create a minimal thread (M1: stub)
KThread* CreateThread(KProcess* process, u64 entry_point, u64 stack_top);

// Current process/thread (M1: single-process stub)
KProcess* CurrentProcess();
KThread*  CurrentThread();

// Terminate
void TerminateProcess(KProcess* process, NTSTATUS exit_status);
void TerminateThread(KThread* thread, NTSTATUS exit_status);

} // namespace PS

// ============================================================
// KProcess structure
// ============================================================
struct KProcess {
    char     Name[64];
    u64      PageDirectoryBase;   // CR3 value (M3)
    u32      Pid;
    NTSTATUS ExitStatus;
    bool     IsTerminated;
};

// ============================================================
// KThread structure
// ============================================================
struct KThread {
    KProcess*   Process;
    u64         Rsp;              // saved stack pointer
    u64         EntryPoint;
    u32         Tid;
    ThreadState State;
    NTSTATUS    ExitStatus;
};
