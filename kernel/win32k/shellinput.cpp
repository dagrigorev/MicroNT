// shellinput.cpp -- MicroNT shell hit-test/input model.

#include "../include/shellinput.h"
#include "../include/debug.h"

namespace SHELLINPUT {

static const char* HitName(HitTargetKind kind) {
    switch (kind) {
    case HitTargetKind::Desktop: return "desktop";
    case HitTargetKind::DesktopIcon: return "desktop-icon";
    case HitTargetKind::ShellWindow: return "shell-window";
    case HitTargetKind::StartButton: return "start-button";
    case HitTargetKind::StartMenu: return "start-menu";
    case HitTargetKind::Taskbar: return "taskbar";
    case HitTargetKind::Tray: return "tray";
    case HitTargetKind::None:
    default: return "none";
    }
}

static bool InRect(u32 px, u32 py, u32 x, u32 y, u32 w, u32 h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

static void ResolveHitTarget(PointerState& pointer,
                             const DESKTOPMODEL::DesktopLayout& layout) {
    pointer.HotTarget = HitTargetKind::Desktop;
    pointer.TargetIndex = 0;

    u32 task_h = 44;
    u32 task_y = 1080 - task_h;
    if (pointer.Y >= task_y) {
        if (InRect(pointer.X, pointer.Y, 8, task_y + 5, 116, task_h - 10)) {
            pointer.HotTarget = HitTargetKind::StartButton;
            return;
        }
        if (pointer.X >= 1920 - 154) {
            pointer.HotTarget = HitTargetKind::Tray;
            return;
        }
        pointer.HotTarget = HitTargetKind::Taskbar;
        pointer.TargetIndex = pointer.X > 136 ? (pointer.X - 136) / 132 : 0;
        return;
    }

    if (layout.StartMenuOpen) {
        u32 menu_w = 345;
        u32 menu_h = 420;
        u32 menu_y = task_y - menu_h;
        if (InRect(pointer.X, pointer.Y, 8, menu_y, menu_w, menu_h)) {
            pointer.HotTarget = HitTargetKind::StartMenu;
            pointer.TargetIndex = pointer.Y > menu_y + 78
                ? (pointer.Y - menu_y - 78) / 34
                : 0;
            return;
        }
    }

    for (u32 i = 0; i < layout.WindowCount; ++i) {
        const DESKTOPMODEL::ShellWindow& win = layout.Windows[i];
        if (InRect(pointer.X, pointer.Y, win.X, win.Y, win.Width, win.Height)) {
            pointer.HotTarget = HitTargetKind::ShellWindow;
            pointer.TargetIndex = i;
            return;
        }
    }

    for (u32 i = 0; i < layout.IconCount; ++i) {
        u32 icon_x = 18;
        u32 icon_y = 14 + i * 82;
        if (InRect(pointer.X, pointer.Y, icon_x, icon_y, 96, 72)) {
            pointer.HotTarget = HitTargetKind::DesktopIcon;
            pointer.TargetIndex = i;
            return;
        }
    }
}

void Init() {
    Debug::Print("[SHELLINPUT] Shell input model initialized\r\n");
}

bool AttachLayout(PointerState& pointer,
                  const INPUTHOST::InputDesktop& input,
                  const DESKTOPMODEL::DesktopLayout& layout) {
    if (!input.FocusedShellSurface || !layout.Ready ||
        input.SessionId != layout.SessionId) {
        return false;
    }

    pointer.SessionId = layout.SessionId;
    pointer.X = 1700;
    pointer.Y = 560;
    pointer.HotTarget = HitTargetKind::None;
    pointer.TargetIndex = 0;
    pointer.HitTestingReady = true;
    pointer.LeftButtonDown = false;
    pointer.ClickDelivered = false;

    Debug::Printf("[SHELLINPUT] Session %u hit-test map ready (%u icons, %u windows)\r\n",
                  pointer.SessionId, layout.IconCount, layout.WindowCount);
    return true;
}

bool PrimeDefaultHitTarget(PointerState& pointer,
                           const DESKTOPMODEL::DesktopLayout& layout) {
    if (!pointer.HitTestingReady) return false;

    pointer.HotTarget = layout.StartMenuOpen ? HitTargetKind::StartMenu
                                             : HitTargetKind::ShellWindow;
    pointer.TargetIndex = 0;
    if (layout.WindowCount > 0) {
        pointer.X = layout.Windows[0].X + 48;
        pointer.Y = layout.Windows[0].Y + 24;
    }
    Debug::Printf("[SHELLINPUT] Session %u default target=%s[%u]\r\n",
                  pointer.SessionId, HitName(pointer.HotTarget),
                  pointer.TargetIndex);
    return true;
}

bool MovePointer(PointerState& pointer,
                 const DESKTOPMODEL::DesktopLayout& layout,
                 u32 x, u32 y) {
    if (!pointer.HitTestingReady || !layout.Ready ||
        pointer.SessionId != layout.SessionId) {
        return false;
    }

    pointer.X = x;
    pointer.Y = y;
    ResolveHitTarget(pointer, layout);

    Debug::Printf("[SHELLINPUT] Session %u pointer move (%u,%u) -> %s[%u]\r\n",
                  pointer.SessionId, pointer.X, pointer.Y,
                  HitName(pointer.HotTarget), pointer.TargetIndex);
    return true;
}

bool ClickPointer(PointerState& pointer,
                  const DESKTOPMODEL::DesktopLayout& layout,
                  u32 x, u32 y) {
    if (!MovePointer(pointer, layout, x, y)) return false;

    pointer.LeftButtonDown = false;
    pointer.ClickDelivered = true;
    Debug::Printf("[SHELLINPUT] Session %u pointer click -> %s[%u]\r\n",
                  pointer.SessionId, HitName(pointer.HotTarget),
                  pointer.TargetIndex);
    return true;
}

} // namespace SHELLINPUT
