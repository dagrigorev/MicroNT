// userinit.cpp -- MicroNT user profile and shell launcher.

#include "../include/userinit.h"
#include "../include/debug.h"
#include "../include/memory.h"
#include "../include/ntstatus.h"
#include "../include/pe.h"

namespace SYSCALL {
void SetCommands(const char** cmds, u32 count);
}

namespace USERINIT {

void Init() {
    Debug::Print("[USERINIT] Userinit subsystem initialized\r\n");
}

bool PrepareInteractiveUser(u32 session_id, PROFILE::UserProfile& profile) {
    KASSERT(profile.SessionId == session_id);
    KASSERT(profile.EnvironmentReady);
    Debug::Printf("[USERINIT] Session %u user environment applied\r\n", session_id);
    SYSCALL::SetCommands(nullptr, 0);
    return true;
}

ShellLaunchResult LaunchShell(u32 session_id, const char* shell_name,
                              const SM::ShellImageConfig& cfg) {
    const char* process_name = shell_name ? shell_name : "explorer.exe";
    Debug::Printf("[USERINIT] Session %u launching shell %s\r\n",
                  session_id, process_name);

    u64 user_cr3 = VMM::CreateUserPml4();
    KASSERT(user_cr3);

    KProcess* proc = PS::CreateProcess(process_name, user_cr3);
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

    return { proc, thread };
}

} // namespace USERINIT
