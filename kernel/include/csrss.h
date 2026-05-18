#pragma once
// csrss.h -- MicroNT Client/Server Runtime Subsystem boundary.

#include "ntdef.h"

namespace CSRSS {

struct Win32Session {
    u32  SessionId;
    bool RuntimeReady;
    bool ConsoleReady;
};

void Init();
bool CreateWin32Session(Win32Session& session, u32 session_id);
bool StartConsoleRuntime(Win32Session& session);

} // namespace CSRSS
