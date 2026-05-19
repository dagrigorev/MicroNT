#pragma once
// windowmgr.h -- MicroNT top-level window manager boundary.

#include "ntdef.h"
#include "shellhost.h"
#include "winsta.h"

namespace WINDOWMGR {

enum class WindowKind : u32 {
    Desktop,
    Shell,
    Taskbar,
};

struct Window {
    u32 Id;
    WindowKind Kind;
    const char* Title;
    bool Visible;
    bool Foreground;
};

struct DesktopScene {
    u32 SessionId;
    Window Windows[4];
    u32 WindowCount;
    bool ZOrderReady;
};

void Init();
bool CreateDesktopScene(DesktopScene& scene, const WINSTA::Desktop& desktop);
bool AttachShellSurface(DesktopScene& scene,
                        const SHELLHOST::ShellSurface& surface);
bool SetForegroundWindow(DesktopScene& scene, u32 window_id);

} // namespace WINDOWMGR
