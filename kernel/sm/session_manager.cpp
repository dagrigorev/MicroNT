// session_manager.cpp -- MicroNT interactive session bootstrap.

#include "../include/session.h"
#include "../include/debug.h"
#include "../include/process.h"
#include "../include/userinit.h"
#include "../include/winlogon.h"
#include "../include/win32k.h"

namespace SM {

static InteractiveSession s_interactive{};
static WIN32K::SessionGraphics s_graphics{};
static WINLOGON::LogonSession s_logon{};
static u32 s_next_session_id = 1;

static bool SmssCreateSession(InteractiveSession& session) {
    session.SessionId = s_next_session_id++;
    session.ShellProcess = nullptr;
    session.ShellThread = nullptr;
    Debug::Printf("[SMSS] Session %u created\r\n", session.SessionId);
    return true;
}

static bool WinlogonStart(InteractiveSession& session) {
    return WINLOGON::CreateLogonSession(s_logon, session.SessionId) &&
           WINLOGON::AcceptAutoLogon(s_logon) &&
           WINLOGON::AllowUserinit(s_logon);
}

static bool UserinitStart(InteractiveSession& session) {
    return USERINIT::PrepareInteractiveUser(session.SessionId);
}

static KThread* ExplorerStart(InteractiveSession& session,
                              const ShellImageConfig& cfg) {
    Debug::Printf("[EXPLORER] Session %u shell bootstrap\r\n", session.SessionId);
    USERINIT::ShellLaunchResult shell =
        USERINIT::LaunchShell(session.SessionId, cfg);
    session.ShellProcess = shell.Process;
    session.ShellThread = shell.Thread;
    return shell.Thread;
}

void Init() {
    s_interactive = {};
    s_graphics = {};
    s_logon = {};
    s_next_session_id = 1;
    Debug::Print("[SMSS] Session manager initialized\r\n");
}

InteractiveSession* StartInteractiveSession(const ShellImageConfig& cfg) {
    InteractiveSession& session = s_interactive;
    KASSERT(SmssCreateSession(session));
    KASSERT(WIN32K::StartCsrss(s_graphics, session.SessionId));
    KASSERT(WinlogonStart(session));
    KASSERT(UserinitStart(session));
    KASSERT(WIN32K::StartDwm(s_graphics));

    KThread* shell = ExplorerStart(session, cfg);

    WIN32K::PresentShellDesktop(s_graphics);

    Sched::AddThread(shell);
    Debug::Printf("[EXPLORER] Session %u shell started\r\n", session.SessionId);
    return &session;
}

} // namespace SM
