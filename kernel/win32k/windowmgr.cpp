// windowmgr.cpp -- MicroNT top-level window manager boundary.

#include "../include/windowmgr.h"
#include "../include/debug.h"

namespace WINDOWMGR {

static Window MakeWindow(u32 id, WindowKind kind, const char* title) {
    Window window{};
    window.Id = id;
    window.Kind = kind;
    window.Title = title;
    window.Visible = true;
    window.Foreground = false;
    return window;
}

void Init() {
    Debug::Print("[WINDOWMGR] Window manager initialized\r\n");
}

bool CreateDesktopScene(DesktopScene& scene, const WINSTA::Desktop& desktop) {
    if (desktop.SessionId == 0) return false;

    scene.SessionId = desktop.SessionId;
    scene.WindowCount = 0;
    scene.ZOrderReady = false;

    scene.Windows[scene.WindowCount++] =
        MakeWindow(1, WindowKind::Desktop, "Progman");

    Debug::Printf("[WINDOWMGR] Session %u desktop scene created\r\n",
                  scene.SessionId);
    return true;
}

bool AttachShellSurface(DesktopScene& scene,
                        const SHELLHOST::ShellSurface& surface) {
    if (surface.SessionId != scene.SessionId || !surface.BoundToExplorer) {
        return false;
    }
    if (scene.WindowCount + 2 > 4) return false;

    scene.Windows[scene.WindowCount++] =
        MakeWindow(2, WindowKind::Shell, "explorer.exe");
    scene.Windows[scene.WindowCount++] =
        MakeWindow(3, WindowKind::Taskbar, "Shell_TrayWnd");
    scene.ZOrderReady = true;

    Debug::Printf("[WINDOWMGR] Session %u shell windows inserted into z-order\r\n",
                  scene.SessionId);
    return true;
}

bool SetForegroundWindow(DesktopScene& scene, u32 window_id) {
    if (!scene.ZOrderReady) return false;

    bool found = false;
    for (u32 i = 0; i < scene.WindowCount; ++i) {
        scene.Windows[i].Foreground = scene.Windows[i].Id == window_id;
        found = found || scene.Windows[i].Foreground;
    }
    if (!found) return false;

    Debug::Printf("[WINDOWMGR] Session %u foreground window=%u\r\n",
                  scene.SessionId, window_id);
    return true;
}

} // namespace WINDOWMGR
