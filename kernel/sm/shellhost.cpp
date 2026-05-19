// shellhost.cpp -- MicroNT ShellExperienceHost boundary.

#include "../include/shellhost.h"
#include "../include/debug.h"

namespace SHELLHOST {

void Init() {
    Debug::Print("[SHELLHOST] ShellExperienceHost initialized\r\n");
}

bool CreateShellSurface(ShellSurface& surface, WINSTA::Desktop& desktop) {
    if (desktop.SessionId == 0) return false;

    surface.SessionId = desktop.SessionId;
    surface.TaskbarReady = true;
    surface.StartSurfaceReady = true;
    surface.TrayReady = true;
    surface.BoundToExplorer = false;

    Debug::Printf("[SHELLHOST] Session %u shell surface created\r\n",
                  surface.SessionId);
    Debug::Printf("[SHELLHOST] Session %u taskbar/start/tray ready\r\n",
                  surface.SessionId);
    return true;
}

bool BindExplorer(ShellSurface& surface, const EXPLORER::Shell& shell) {
    if (!shell.Registered || shell.SessionId != surface.SessionId) return false;

    surface.BoundToExplorer = true;
    Debug::Printf("[SHELLHOST] Session %u bound to explorer.exe\r\n",
                  surface.SessionId);
    return true;
}

bool ComposeDesktop(ShellSurface& surface) {
    if (!surface.TaskbarReady || !surface.StartSurfaceReady ||
        !surface.TrayReady || !surface.BoundToExplorer) {
        return false;
    }

    Debug::Printf("[SHELLHOST] Session %u desktop surface composed\r\n",
                  surface.SessionId);
    return true;
}

} // namespace SHELLHOST
