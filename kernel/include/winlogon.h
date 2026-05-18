#pragma once
// winlogon.h -- MicroNT interactive logon owner.

#include "ntdef.h"

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
bool CreateLogonSession(LogonSession& logon, u32 session_id);
bool AcceptAutoLogon(LogonSession& logon);
bool AllowUserinit(LogonSession& logon);

} // namespace WINLOGON
