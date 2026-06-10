// session_manager.cpp -- MicroNT interactive session bootstrap.

#include "../include/session.h"
#include "../include/appmodel.h"
#include "../include/csrss.h"
#include "../include/debug.h"
#include "../include/desktopmodel.h"
#include "../include/displaycfg.h"
#include "../include/dwm.h"
#include "../include/explorer.h"
#include "../include/hal.h"
#include "../include/inputhost.h"
#include "../include/process.h"
#include "../include/profile.h"
#include "../include/registry.h"
#include "../include/services.h"
#include "../include/shellcommands.h"
#include "../include/shellhost.h"
#include "../include/shellact.h"
#include "../include/shella11y.h"
#include "../include/shellinput.h"
#include "../include/shellnotify.h"
#include "../include/shellplaces.h"
#include "../include/shellpower.h"
#include "../include/shellstart.h"
#include "../include/shelltaskbar.h"
#include "../include/shelltray.h"
#include "../include/uxtheme.h"
#include "../include/userinit.h"
#include "../include/winlogon.h"
#include "../include/windowmgr.h"
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
static SHELLHOST::ShellSurface s_shell_surface{};
static INPUTHOST::InputDesktop s_input_desktop{};
static WINDOWMGR::DesktopScene s_desktop_scene{};
static DESKTOPMODEL::DesktopLayout s_desktop_layout{};
static DISPLAYCFG::DisplayMode s_display_mode{};
static DISPLAYCFG::DisplayTarget s_display_target{};
static UXTHEME::Theme s_theme{};
static SHELLINPUT::PointerState s_pointer_state{};
static SHELLACT::ActivationState s_activation_state{};
static SHELLA11Y::AccessibilityState s_accessibility_state{};
static SHELLCOMMANDS::CommandState s_command_state{};
static SHELLTRAY::TrayState s_tray_state{};
static SHELLNOTIFY::NotificationQueue s_notification_queue{};
static SHELLSTART::StartMenuState s_start_menu{};
static SHELLPLACES::PlaceState s_place_state{};
static SHELLPOWER::PowerState s_power_state{};
static SHELLTASKBAR::TaskbarState s_taskbar_state{};
static WINLOGON::LogonSession s_logon{};
static PROFILE::UserProfile s_profile{};
static APPMODEL::AppIdentity s_shell_app{};
static u32 s_next_session_id = 1;

static constexpr const char* WINLOGON_KEY =
    "\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";

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
    const char* userinit = REGISTRY::QueryString(WINLOGON_KEY, "Userinit");
    Debug::Printf("[SMSS] Session %u starting %s\r\n",
                  session.SessionId, userinit ? userinit : "userinit.exe");

    return PROFILE::LoadUserProfile(s_profile, session.SessionId, "DefaultUser") &&
           PROFILE::ApplyEnvironment(s_profile) &&
           USERINIT::PrepareInteractiveUser(session.SessionId, s_profile);
}

static bool ExplorerStart(InteractiveSession& session,
                          const ShellImageConfig& cfg) {
    Debug::Printf("[EXPLORER] Session %u shell bootstrap\r\n", session.SessionId);
    const char* shell_name = REGISTRY::QueryString(WINLOGON_KEY, "Shell");
    const char* image_name = shell_name ? shell_name : "explorer.exe";
    KASSERT(APPMODEL::RegisterShellApp(s_shell_app, session.SessionId, image_name));
    KASSERT(APPMODEL::ActivateShellApp(s_shell_app));
    USERINIT::ShellLaunchResult shell =
        USERINIT::LaunchShell(session.SessionId, shell_name, s_shell_app, cfg);
    session.ShellProcess = shell.Process;
    session.ShellThread = shell.Thread;
    return EXPLORER::RegisterShell(s_shell, session.SessionId,
                                   shell.Process, shell.Thread);
}

static bool ShellHostStart(InteractiveSession& session) {
    return SHELLHOST::CreateShellSurface(s_shell_surface, s_shell_desktop) &&
           SHELLHOST::BindExplorer(s_shell_surface, s_shell) &&
           SHELLHOST::ComposeDesktop(s_shell_surface);
}

static bool WindowManagerStart(InteractiveSession& session) {
    return WINDOWMGR::CreateDesktopScene(s_desktop_scene, s_shell_desktop) &&
           WINDOWMGR::AttachShellSurface(s_desktop_scene, s_shell_surface) &&
           WINDOWMGR::SetForegroundWindow(s_desktop_scene, 2);
}

static bool DesktopModelStart(InteractiveSession& session) {
    if (!DESKTOPMODEL::BuildXpRedesignLayout(s_desktop_layout, s_desktop_scene))
        return false;
    // Pin the layout to the real display target so the renderer and the pointer
    // hit-test share one set of screen dimensions.
    if (s_display_target.MetricsReady) {
        s_desktop_layout.ScreenW = s_display_target.Mode.Width;
        s_desktop_layout.ScreenH = s_display_target.Mode.Height;
    }
    return true;
}

static bool InputHostStart(InteractiveSession& session) {
    return INPUTHOST::AttachDesktop(s_input_desktop, s_shell_desktop) &&
           INPUTHOST::FocusShellSurface(s_input_desktop, s_shell_surface) &&
           INPUTHOST::ShowCursor(s_input_desktop);
}

static bool ShellInputStart(InteractiveSession& session) {
    return SHELLINPUT::AttachLayout(s_pointer_state, s_input_desktop,
                                    s_desktop_layout) &&
           SHELLINPUT::PrimeDefaultHitTarget(s_pointer_state, s_desktop_layout) &&
           SHELLINPUT::ClickPointer(s_pointer_state, s_desktop_layout, 1040, 82);
}

static bool ShellActivationStart(InteractiveSession& session) {
    return SHELLACT::CreateActivationState(s_activation_state, s_desktop_layout) &&
           SHELLACT::ApplyPointerTarget(s_activation_state, s_pointer_state,
                                        s_desktop_layout) &&
           SHELLACT::PublishTaskbarState(s_activation_state, s_desktop_layout);
}

static bool ShellAccessibilityStart(InteractiveSession& session) {
    return SHELLA11Y::CreateAccessibilityState(s_accessibility_state,
                                               s_activation_state) &&
           SHELLA11Y::PublishFocusCues(s_accessibility_state,
                                       s_activation_state);
}

static bool ShellTrayStart(InteractiveSession& session) {
    return SHELLTRAY::CreateTrayState(s_tray_state, s_activation_state) &&
           SHELLTRAY::PublishTrayState(s_tray_state);
}

static bool ShellStartMenuStart(InteractiveSession& session) {
    return SHELLSTART::BuildStartMenu(s_start_menu, s_desktop_layout,
                                      s_activation_state) &&
           SHELLSTART::PublishStartMenu(s_start_menu);
}

static bool ShellTaskbarStart(InteractiveSession& session) {
    return SHELLTASKBAR::BuildTaskbar(s_taskbar_state, s_desktop_layout,
                                      s_activation_state) &&
           SHELLTASKBAR::PublishTaskbar(s_taskbar_state);
}

static bool ShellPowerStart(InteractiveSession& session) {
    return SHELLPOWER::BuildPowerState(s_power_state, s_start_menu) &&
           SHELLPOWER::PublishPowerState(s_power_state);
}

static bool ShellPlacesStart(InteractiveSession& session) {
    return SHELLPLACES::BuildPlaceState(s_place_state, s_desktop_layout,
                                        s_start_menu) &&
           SHELLPLACES::PublishPlaceState(s_place_state);
}

static bool ShellCommandsStart(InteractiveSession& session) {
    return SHELLCOMMANDS::BuildCommandState(s_command_state, s_start_menu) &&
           SHELLCOMMANDS::PublishCommandState(s_command_state);
}

static bool ShellNotificationsStart(InteractiveSession& session) {
    return SHELLNOTIFY::CreateNotificationQueue(s_notification_queue,
                                                s_tray_state) &&
           SHELLNOTIFY::PublishWelcomeNotification(s_notification_queue);
}

static bool DisplayConfigStart(InteractiveSession& session) {
    return DISPLAYCFG::QueryPrimaryMode(s_display_mode) &&
           DISPLAYCFG::AttachSessionTarget(s_display_target, session.SessionId,
                                           s_display_mode) &&
           DISPLAYCFG::ApplyDesktopMetrics(s_display_target);
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
    s_shell_surface = {};
    s_input_desktop = {};
    s_desktop_scene = {};
    s_desktop_layout = {};
    s_display_mode = {};
    s_display_target = {};
    s_theme = {};
    s_pointer_state = {};
    s_activation_state = {};
    s_accessibility_state = {};
    s_command_state = {};
    s_tray_state = {};
    s_notification_queue = {};
    s_start_menu = {};
    s_place_state = {};
    s_power_state = {};
    s_taskbar_state = {};
    s_logon = {};
    s_profile = {};
    s_shell_app = {};
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
    KASSERT(DisplayConfigStart(session));

    KASSERT(ExplorerStart(session, cfg));
    KASSERT(UXTHEME::LoadDefaultTheme(s_theme));
    KASSERT(ShellHostStart(session));
    KASSERT(WindowManagerStart(session));
    KASSERT(DesktopModelStart(session));

    DWM::PresentShellDesktop(s_compositor, s_shell_desktop, s_desktop_scene,
                             s_display_target, s_desktop_layout, s_theme);

    KASSERT(InputHostStart(session));
    KASSERT(ShellInputStart(session));
    KASSERT(ShellActivationStart(session));
    KASSERT(ShellAccessibilityStart(session));
    KASSERT(ShellStartMenuStart(session));
    KASSERT(ShellPlacesStart(session));
    KASSERT(ShellPowerStart(session));
    KASSERT(ShellCommandsStart(session));
    KASSERT(ShellTaskbarStart(session));
    KASSERT(ShellTrayStart(session));
    KASSERT(ShellNotificationsStart(session));
    KASSERT(EXPLORER::StartShellThread(s_shell));
    return &session;
}

// Recompose the desktop and put the cursor back where the pointer is
// (StartDesktop parks it at a default spot).
static void RepaintDesktop() {
    DWM::PresentShellDesktop(s_compositor, s_shell_desktop, s_desktop_scene,
                             s_display_target, s_desktop_layout, s_theme);
    VGA::MoveMouseCursor(s_pointer_state.X, s_pointer_state.Y);
}

static void SetStartMenu(bool open) {
    s_desktop_layout.StartMenuOpen = open;
    s_activation_state.StartMenuOpen = open;
    s_start_menu.Open = open;
}

// Open a cascaded window with the given title (top of the z-order).
static void OpenWindow(const char* title) {
    DESKTOPMODEL::DesktopLayout& L = s_desktop_layout;
    if (L.WindowCount >= 8) {                 // recycle the oldest slot
        for (u32 i = 1; i < L.WindowCount; ++i) L.Windows[i - 1] = L.Windows[i];
        --L.WindowCount;
    }
    u32 i = L.WindowCount;
    u32 cascade = i * 28;
    DESKTOPMODEL::ShellWindow w{};
    w.Title  = title ? title : "Window";
    w.Status = "MicroNT window";
    w.X = 320 + cascade;
    w.Y = 140 + cascade;
    w.Width = 560;
    w.Height = 360;
    w.Toolbar = true;
    L.Windows[i] = w;
    L.WindowCount = i + 1;
    Debug::Printf("[SMSS] opened window '%s' (%u open)\r\n", w.Title, L.WindowCount);
}

static void CloseWindow(u32 idx) {
    DESKTOPMODEL::DesktopLayout& L = s_desktop_layout;
    if (idx >= L.WindowCount) return;
    Debug::Printf("[SMSS] closed window '%s'\r\n",
                  L.Windows[idx].Title ? L.Windows[idx].Title : "?");
    for (u32 i = idx + 1; i < L.WindowCount; ++i) L.Windows[i - 1] = L.Windows[i];
    --L.WindowCount;
}

// Raise a window to the top of the z-order (drawn last / on top).
static void FocusWindow(u32 idx) {
    DESKTOPMODEL::DesktopLayout& L = s_desktop_layout;
    if (idx + 1 >= L.WindowCount) return;     // already on top or invalid
    DESKTOPMODEL::ShellWindow w = L.Windows[idx];
    for (u32 i = idx + 1; i < L.WindowCount; ++i) L.Windows[i - 1] = L.Windows[i];
    L.Windows[L.WindowCount - 1] = w;
    Debug::Printf("[SMSS] raised window '%s'\r\n", w.Title ? w.Title : "?");
}

static void ToggleStartMenu() {
    bool open = !s_desktop_layout.StartMenuOpen;
    SetStartMenu(open);
    Debug::Printf("[SMSS] Session %u Start menu %s\r\n",
                  s_interactive.SessionId, open ? "opened" : "closed");
    RepaintDesktop();
}

static const char* const kStartApps[8] = {
    "Edge", "Files", "Settings", "Store", "Photos", "Mail", "Notepad", "Terminal"
};

static void HandleShellClick(SHELLINPUT::HitTargetKind target, u32 index) {
    using K = SHELLINPUT::HitTargetKind;
    switch (target) {
    case K::StartButton:
        ToggleStartMenu();
        break;
    case K::StartMenu:                        // launch a pinned app, close menu
        OpenWindow(kStartApps[index < 8 ? index : 0]);
        SetStartMenu(false);
        RepaintDesktop();
        break;
    case K::DesktopIcon:                      // open a window for the icon
        if (index < s_desktop_layout.IconCount)
            OpenWindow(s_desktop_layout.Icons[index].Label);
        RepaintDesktop();
        break;
    case K::WindowClose:                      // close the window
        CloseWindow(index);
        RepaintDesktop();
        break;
    case K::ShellWindow:                      // raise/focus the window
    case K::Taskbar:                          // taskbar button -> raise its window
        FocusWindow(index);
        RepaintDesktop();
        break;
    default:
        break;
    }
}

// Window-drag state: a window is dragged by holding the left button on its
// title bar (the 32 px strip below the top edge, excluding the [x] button).
static bool s_dragging = false;
static u32  s_drag_win = 0;
static i32  s_drag_off_x = 0;
static i32  s_drag_off_y = 0;

bool PumpInteractiveInput() {
    if (!s_pointer_state.HitTestingReady || !s_desktop_layout.Ready) return false;

    bool changed = false;
    MOUSE::Packet packet{};
    while (MOUSE::TryRead(&packet)) {
        i32 mx = 0, my = 0;
        if (!MOUSE::CurrentPosition(&mx, &my)) break;

        // Any mouse packet moved the cursor, which the IRQ already drew, so the
        // frame needs flushing.
        changed = true;

        bool prev_down = s_pointer_state.LeftButtonDown;
        SHELLINPUT::PointerEvent event = SHELLINPUT::ProcessPointer(
            s_pointer_state, s_desktop_layout,
            static_cast<u32>(mx), static_cast<u32>(my), packet.Left);
        bool down_now = packet.Left;

        // Active drag: move the window with the cursor (grab point fixed),
        // or end the drag on release.
        if (s_dragging) {
            if (down_now && s_drag_win < s_desktop_layout.WindowCount) {
                DESKTOPMODEL::ShellWindow& w = s_desktop_layout.Windows[s_drag_win];
                i32 nx = mx - s_drag_off_x;
                i32 ny = my - s_drag_off_y;
                w.X = nx > 0 ? (u32)nx : 0;
                w.Y = ny > 0 ? (u32)ny : 0;
                RepaintDesktop();
            } else {
                s_dragging = false;
                RepaintDesktop();
            }
            continue;
        }

        // Press on a title bar -> raise the window and begin dragging it.
        if (!prev_down && down_now &&
            s_pointer_state.HotTarget == SHELLINPUT::HitTargetKind::ShellWindow) {
            u32 i = s_pointer_state.TargetIndex;
            if (i < s_desktop_layout.WindowCount &&
                (u32)my < s_desktop_layout.Windows[i].Y + 32) {
                FocusWindow(i);                          // raise to top
                s_drag_win = s_desktop_layout.WindowCount - 1;
                const DESKTOPMODEL::ShellWindow& w =
                    s_desktop_layout.Windows[s_drag_win];
                s_drag_off_x = mx - (i32)w.X;
                s_drag_off_y = my - (i32)w.Y;
                s_dragging = true;
                RepaintDesktop();
                continue;
            }
        }

        // Otherwise a normal release-edge click.
        if (event.Clicked) {
            HandleShellClick(event.Target, event.TargetIndex);
        }
    }
    return changed;
}

} // namespace SM
