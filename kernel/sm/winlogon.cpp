// winlogon.cpp -- MicroNT interactive logon owner.

#include "../include/winlogon.h"
#include "../include/debug.h"

namespace WINLOGON {

void Init() {
    Debug::Print("[WINLOGON] Winlogon subsystem initialized\r\n");
}

bool CreateLogonSession(LogonSession& logon, u32 session_id) {
    logon.SessionId = session_id;
    logon.State = LogonState::WaitingForCredentials;
    Debug::Printf("[WINLOGON] Session %u secure logon desktop ready\r\n",
                  session_id);
    return true;
}

bool AcceptAutoLogon(LogonSession& logon) {
    if (logon.State != LogonState::WaitingForCredentials) return false;
    logon.State = LogonState::Authenticated;
    Debug::Printf("[WINLOGON] Session %u credentials accepted\r\n",
                  logon.SessionId);
    return true;
}

bool AllowUserinit(LogonSession& logon) {
    if (logon.State != LogonState::Authenticated) return false;
    logon.State = LogonState::UserinitAllowed;
    Debug::Printf("[WINLOGON] Session %u userinit allowed\r\n",
                  logon.SessionId);
    return true;
}

} // namespace WINLOGON
