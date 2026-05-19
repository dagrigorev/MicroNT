// winlogon.cpp -- MicroNT interactive logon owner.

#include "../include/winlogon.h"
#include "../include/debug.h"

namespace WINLOGON {

void Init() {
    Debug::Print("[WINLOGON] Winlogon subsystem initialized\r\n");
}

bool CreateLogonSession(LogonSession& logon, WINSTA::Desktop& secure_desktop) {
    if (!WINSTA::SwitchDesktop(secure_desktop)) return false;
    logon.SessionId = secure_desktop.SessionId;
    logon.State = LogonState::WaitingForCredentials;
    Debug::Printf("[WINLOGON] Session %u secure logon desktop ready\r\n",
                  secure_desktop.SessionId);
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
