#pragma once
// winlogon.h -- MicroNT interactive logon owner.

#include "ntdef.h"
#include "winsta.h"

namespace WINLOGON {

enum class LogonState : u32 {
    Created = 0,
    WaitingForCredentials,
    Authenticated,
    UserinitAllowed,
};

struct LogonSession {
    u32        SessionId;
    LogonState State;
};

void Init();
bool CreateLogonSession(LogonSession& logon, WINSTA::Desktop& secure_desktop);
bool AcceptAutoLogon(LogonSession& logon);
bool AllowUserinit(LogonSession& logon);

} // namespace WINLOGON
