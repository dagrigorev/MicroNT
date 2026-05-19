// session_manager.cpp -- MicroNT interactive session bootstrap.

#include "../include/session.h"
#include "../include/csrss.h"
#include "../include/debug.h"
#include "../include/dwm.h"
#include "../include/explorer.h"
#include "../include/process.h"
#include "../include/profile.h"
#include "../include/services.h"
#include "../include/userinit.h"
#include "../include/winlogon.h"
#include "../include/winsta.h"
#include "../include/win32k.h"

namespace SM {

static SystemSession s_system{};
static SERVICES::ServiceControlPlane s_services{};
static InteractiveSession s_interactive{};
static CSRSS::Win32Session s_win32{};
static WIN32K::SessionGraphics s_graphics{};
static WINSTA::WindowStation s_winsta{};
static WINSTA::Desktop s_secure_desktop{};
static WINSTA::Desktop s_shell_desktop{};
static DWM::Compositor s_compositor{};
static EXPLORER::Shell s_shell{};
static WINLOGON::LogonSession s_logon{};
static PROFILE::UserProfile s_profile{};
static u32 s_next_session_id = 1;

static bool SmssCreateSystemSession(SystemSession& session) {
    session.SessionId = 0;
    bool ok = SERVICES::StartControlPlane(s_services, session.SessionId) &&
              SERVICES::StartCoreServices(s_services);
    session.ServicesReady = s_services.ControlPlaneReady;
    session.CoreServicesStarted = s_services.CoreServicesStarted;
    if (ok) Debug::Print("[SMSS] Session 0 system services ready\r\n");
    return ok;
}

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
    return WINLOGON::CreateLogonSession(s_logon, s_secure_desktop) &&
           WINLOGON::AcceptAutoLogon(s_logon) &&
           WINLOGON::AllowUserinit(s_logon);
}

static bool UserinitStart(InteractiveSession& session) {
    return PROFILE::LoadUserProfile(s_profile, session.SessionId, "DefaultUser") &&
           PROFILE::ApplyEnvironment(s_profile) &&
           USERINIT::PrepareInteractiveUser(session.SessionId, s_profile);
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
    s_system = {};
    s_services = {};
    s_interactive = {};
    s_win32 = {};
    s_graphics = {};
    s_winsta = {};
    s_secure_desktop = {};
    s_shell_desktop = {};
    s_compositor = {};
    s_shell = {};
    s_logon = {};
    s_profile = {};
    s_next_session_id = 1;
    Debug::Print("[SMSS] Session manager initialized\r\n");
    KASSERT(SmssCreateSystemSession(s_system));
}

InteractiveSession* StartInteractiveSession(const ShellImageConfig& cfg) {
    InteractiveSession& session = s_interactive;
    KASSERT(SmssCreateSession(session));
    KASSERT(CsrssStart(session));
    KASSERT(WIN32K::AttachSession(s_graphics, s_win32));
    KASSERT(WINSTA::CreateInteractiveWindowStation(s_winsta, session.SessionId));
    KASSERT(WINSTA::CreateDesktop(s_secure_desktop, s_winsta,
                                  WINSTA::DesktopKind::SecureLogon,
                                  "Winlogon"));
    KASSERT(WINSTA::CreateDesktop(s_shell_desktop, s_winsta,
                                  WINSTA::DesktopKind::Shell,
                                  "Default"));
    KASSERT(WinlogonStart(session));
    KASSERT(UserinitStart(session));
    KASSERT(DWM::Start(s_compositor, s_graphics));

    KASSERT(ExplorerStart(session, cfg));

    DWM::PresentShellDesktop(s_compositor, s_shell_desktop);

    KASSERT(EXPLORER::StartShellThread(s_shell));
    return &session;
}

} // namespace SM
