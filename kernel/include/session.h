#pragma once
// session.h -- MicroNT session/logon/shell bootstrap layer.
//
// This is intentionally modeled after the Windows boot-to-desktop order:
// SMSS creates the interactive session, Winlogon owns logon, Userinit loads
// user context, and the shell process owns the desktop.

#include "ntdef.h"
#include "process.h"

namespace SM {

struct ShellImageConfig {
    const u8* NtdllImage;
    usize     NtdllSize;
    u64       NtdllBase;

    const u8* ShellImage;
    usize     ShellSize;
    u64       ShellBase;
};

struct InteractiveSession {
    u32       SessionId;
    KProcess* ShellProcess;
    KThread*  ShellThread;
};

void Init();
InteractiveSession* StartInteractiveSession(const ShellImageConfig& cfg);

} // namespace SM
