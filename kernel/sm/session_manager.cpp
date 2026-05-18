// session_manager.cpp -- MicroNT interactive session bootstrap.

#include "../include/session.h"
#include "../include/debug.h"
#include "../include/memory.h"
#include "../include/ntstatus.h"
#include "../include/pe.h"
#include "../include/process.h"
#include "../include/win32k.h"

namespace SYSCALL {
void SetCommands(const char** cmds, u32 count);
}

namespace SM {

static InteractiveSession s_interactive{};
static WIN32K::SessionGraphics s_graphics{};
static u32 s_next_session_id = 1;

static bool SmssCreateSession(InteractiveSession& session) {
    session.SessionId = s_next_session_id++;
    session.ShellProcess = nullptr;
    session.ShellThread = nullptr;
    Debug::Printf("[SMSS] Session %u created\r\n", session.SessionId);
    return true;
}

static bool WinlogonStart(InteractiveSession& session) {
    Debug::Printf("[WINLOGON] Session %u secure logon ready\r\n", session.SessionId);
    return true;
}

static bool UserinitStart(InteractiveSession& session) {
    Debug::Printf("[USERINIT] Session %u user profile initialized\r\n", session.SessionId);
    return true;
}

static KThread* ExplorerStart(InteractiveSession& session,
                              const ShellImageConfig& cfg) {
    Debug::Printf("[EXPLORER] Session %u shell bootstrap\r\n", session.SessionId);

    SYSCALL::SetCommands(nullptr, 0);

    u64 user_cr3 = VMM::CreateUserPml4();
    KASSERT(user_cr3);

    KProcess* proc = PS::CreateProcess("explorer.exe", user_cr3);
    KASSERT(proc);

    u64 ntdll_entry = 0;
    NTSTATUS st = LDR::LoadAndRegister("ntdll.dll",
                                       cfg.NtdllImage, cfg.NtdllSize,
                                       user_cr3, cfg.NtdllBase,
                                       &ntdll_entry);
    KASSERT(NT_SUCCESS(st));

    u64 entry_va = 0;
    st = LDR::LoadPe(cfg.ShellImage, cfg.ShellSize,
                     user_cr3, cfg.ShellBase, &entry_va);
    KASSERT(NT_SUCCESS(st));

    constexpr u64 INTERACTIVE_STACK_VA = 0x9000500000ULL;
    u64 stk_phys = PMM::AllocPage();
    KASSERT(stk_phys);
    for (u32 i = 0; i < PAGE_SIZE; ++i)
        reinterpret_cast<u8*>(stk_phys)[i] = 0;

    bool mapped = VMM::MapPageInto(user_cr3, INTERACTIVE_STACK_VA, stk_phys,
                                   VMM::PTE_PRESENT |
                                   VMM::PTE_WRITABLE |
                                   VMM::PTE_USER);
    KASSERT(mapped);

    KThread* thread = PS::CreateUserThread(
        proc, "explorer.exe!shell", entry_va, INTERACTIVE_STACK_VA + PAGE_SIZE);
    KASSERT(thread);

    session.ShellProcess = proc;
    session.ShellThread = thread;
    return thread;
}

void Init() {
    s_interactive = {};
    s_graphics = {};
    s_next_session_id = 1;
    Debug::Print("[SMSS] Session manager initialized\r\n");
}

InteractiveSession* StartInteractiveSession(const ShellImageConfig& cfg) {
    InteractiveSession& session = s_interactive;
    KASSERT(SmssCreateSession(session));
    KASSERT(WIN32K::StartCsrss(s_graphics, session.SessionId));
    KASSERT(WinlogonStart(session));
    KASSERT(UserinitStart(session));
    KASSERT(WIN32K::StartDwm(s_graphics));

    KThread* shell = ExplorerStart(session, cfg);

    WIN32K::PresentShellDesktop(s_graphics);

    Sched::AddThread(shell);
    Debug::Printf("[EXPLORER] Session %u shell started\r\n", session.SessionId);
    return &session;
}

} // namespace SM
