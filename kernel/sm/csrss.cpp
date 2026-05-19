// csrss.cpp -- MicroNT Client/Server Runtime Subsystem boundary.

#include "../include/csrss.h"
#include "../include/debug.h"

namespace CSRSS {

void Init() {
    Debug::Print("[CSRSS] Client/server runtime initialized\r\n");
}

bool CreateWin32Session(Win32Session& session, u32 session_id) {
    session.SessionId = session_id;
    session.RuntimeReady = true;
    session.ConsoleReady = false;
    Debug::Printf("[CSRSS] Session %u Win32 runtime ready\r\n", session_id);
    return true;
}

bool StartConsoleRuntime(Win32Session& session) {
    if (!session.RuntimeReady) return false;
    session.ConsoleReady = true;
    Debug::Printf("[CSRSS] Session %u console runtime ready\r\n",
                  session.SessionId);
    return true;
}

} // namespace CSRSS
