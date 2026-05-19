// explorer.cpp -- MicroNT shell owner boundary.

#include "../include/explorer.h"
#include "../include/debug.h"

namespace EXPLORER {

void Init() {
    Debug::Print("[EXPLORER] Shell subsystem initialized\r\n");
}

bool RegisterShell(Shell& shell, u32 session_id,
                   KProcess* process, KThread* thread) {
    if (!process || !thread) return false;
    shell.SessionId = session_id;
    shell.Process = process;
    shell.Thread = thread;
    shell.Registered = true;
    Debug::Printf("[EXPLORER] Session %u shell process registered\r\n",
                  session_id);
    return true;
}

bool StartShellThread(Shell& shell) {
    if (!shell.Registered || !shell.Thread) return false;
    Sched::AddThread(shell.Thread);
    Debug::Printf("[EXPLORER] Session %u shell started\r\n", shell.SessionId);
    return true;
}

} // namespace EXPLORER
