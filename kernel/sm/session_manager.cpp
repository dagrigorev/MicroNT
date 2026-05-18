// session_manager.cpp -- MicroNT interactive session bootstrap.

#include "../include/session.h"
#include "../include/csrss.h"
#include "../include/debug.h"
#include "../include/dwm.h"
#include "../include/explorer.h"
#include "../include/process.h"
#include "../include/userinit.h"
#include "../include/winlogon.h"
#include "../include/win32k.h"

namespace SM {

static InteractiveSession s_interactive{};
static CSRSS::Win32Session s_win32{};
static WIN32K::SessionGraphics s_graphics{};
static DWM::Compositor s_compositor{};
static EXPLORER::Shell s_shell{};
static WINLOGON::LogonSession s_logon{};
static u32 s_next_session_id = 1;

static bool SmssCreateSession(InteractiveSession& session) {
    session.SessionId = s_next_session_id++;
    session.ShellProcess = nullptr;
    session.ShellThread = nullptr;
    Debug::Printf("[SMSS] Session %u created\r\n", session.SessionId);
    return true;
}

static bool CsrssStart(InteractiveSession& session) {
    return CSRSS::CreateWin32Session(s_win32, session.SessionId) &&
           CSRSS::StartConsoleRuntime(s_win32);
}

static bool WinlogonStart(InteractiveSession& session) {
    return WINLOGON::CreateLogonSession(s_logon, session.SessionId) &&
           WINLOGON::AcceptAutoLogon(s_logon) &&
           WINLOGON::AllowUserinit(s_logon);
}

static bool UserinitStart(InteractiveSession& session) {
    return USERINIT::PrepareInteractiveUser(session.SessionId);
}

static bool ExplorerStart(InteractiveSession& session,
                          const ShellImageConfig& cfg) {
    Debug::Printf("[EXPLORER] Session %u shell bootstrap\r\n", session.SessionId);
    USERINIT::ShellLaunchResult shell =
        USERINIT::LaunchShell(session.SessionId, cfg);
    session.ShellProcess = shell.Process;
    session.ShellThread = shell.Thread;
    return EXPLORER::RegisterShell(s_shell, session.SessionId,
                                   shell.Process, shell.Thread);
}

void Init() {
    s_interactive = {};
    s_win32 = {};
    s_graphics = {};
    s_compositor = {};
    s_shell = {};
    s_logon = {};
    s_next_session_id = 1;
    Debug::Print("[SMSS] Session manager initialized\r\n");
}

InteractiveSession* StartInteractiveSession(const ShellImageConfig& cfg) {
    InteractiveSession& session = s_interactive;
    KASSERT(SmssCreateSession(session));
    KASSERT(CsrssStart(session));
    KASSERT(WIN32K::AttachSession(s_graphics, s_win32));
    KASSERT(WinlogonStart(session));
    KASSERT(UserinitStart(session));
    KASSERT(DWM::Start(s_compositor, s_graphics));

    KASSERT(ExplorerStart(session, cfg));

    DWM::PresentShellDesktop(s_compositor);

    KASSERT(EXPLORER::StartShellThread(s_shell));
    return &session;
}

} // namespace SM
