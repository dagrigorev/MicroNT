#pragma once
// explorer.h -- MicroNT shell owner boundary.

#include "ntdef.h"
#include "process.h"

namespace EXPLORER {

struct Shell {
    u32       SessionId;
    KProcess* Process;
    KThread*  Thread;
    bool      Registered;
};

void Init();
bool RegisterShell(Shell& shell, u32 session_id, KProcess* process, KThread* thread);
bool StartShellThread(Shell& shell);

} // namespace EXPLORER
