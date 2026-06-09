// shellinput.cpp -- MicroNT shell hit-test/input model.

#include "../include/shellinput.h"
#include "../include/debug.h"

namespace SHELLINPUT {

static const char* HitName(HitTargetKind kind) {
    switch (kind) {
    case HitTargetKind::Desktop: return "desktop";
    case HitTargetKind::DesktopIcon: return "desktop-icon";
    case HitTargetKind::ShellWindow: return "shell-window";
    case HitTargetKind::WindowClose: return "window-close";
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

    DESKTOPMODEL::TaskbarMetrics tb =
        DESKTOPMODEL::ComputeTaskbar(layout.ScreenW, layout.ScreenH,
                                     layout.WindowCount);
    if (pointer.Y >= tb.Y) {
        if (InRect(pointer.X, pointer.Y, tb.StartX, tb.Y, tb.StartW, tb.Height)) {
            pointer.HotTarget = HitTargetKind::StartButton;
            return;
        }
        if (pointer.X >= tb.TrayX) {
            pointer.HotTarget = HitTargetKind::Tray;
            return;
        }
        u32 slot = tb.ButtonW + tb.ButtonGap;
        if (pointer.X >= tb.FirstButtonX &&
            pointer.X < tb.FirstButtonX + tb.ButtonCount * slot) {
            pointer.HotTarget = HitTargetKind::Taskbar;
            pointer.TargetIndex = (pointer.X - tb.FirstButtonX) / slot;
        } else {
            pointer.HotTarget = HitTargetKind::Taskbar;
            pointer.TargetIndex = 0;
        }
        return;
    }

    if (layout.StartMenuOpen) {
        // Win11 Start: a centered flyout anchored above the centered taskbar.
        u32 menu_w = 640;
        u32 menu_h = 720;
        u32 menu_x = layout.ScreenW > menu_w ? (layout.ScreenW - menu_w) / 2 : 0;
        u32 menu_y = tb.Y > menu_h + 12 ? tb.Y - menu_h - 12 : 0;
        if (InRect(pointer.X, pointer.Y, menu_x, menu_y, menu_w, menu_h)) {
            pointer.HotTarget = HitTargetKind::StartMenu;
            pointer.TargetIndex = pointer.Y > menu_y + 120
                ? (pointer.Y - menu_y - 120) / 96
                : 0;
            return;
        }
    }

    // Windows are drawn in array order (later = on top), so hit-test the
    // top-most first by iterating in reverse. The [x] close button (top-right
    // of the 32 px title bar, ~46 px wide) takes priority over the body.
    for (i32 i = (i32)layout.WindowCount - 1; i >= 0; --i) {
        const DESKTOPMODEL::ShellWindow& win = layout.Windows[i];
        if (InRect(pointer.X, pointer.Y, win.X + win.Width - 46, win.Y, 46, 32)) {
            pointer.HotTarget = HitTargetKind::WindowClose;
            pointer.TargetIndex = (u32)i;
            return;
        }
        if (InRect(pointer.X, pointer.Y, win.X, win.Y, win.Width, win.Height)) {
            pointer.HotTarget = HitTargetKind::ShellWindow;
            pointer.TargetIndex = (u32)i;
            return;
        }
    }

    for (u32 i = 0; i < layout.IconCount; ++i) {
        u32 icon_x = 18;
        u32 icon_y = 14 + i * 92;
        if (InRect(pointer.X, pointer.Y, icon_x, icon_y, 96, 84)) {
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

PointerEvent ProcessPointer(PointerState& pointer,
                            const DESKTOPMODEL::DesktopLayout& layout,
                            u32 x, u32 y, bool left_down) {
    PointerEvent event{};
    event.Target = HitTargetKind::None;

    if (!pointer.HitTestingReady || !layout.Ready ||
        pointer.SessionId != layout.SessionId) {
        return event;
    }

    HitTargetKind prev_target = pointer.HotTarget;
    u32 prev_index = pointer.TargetIndex;

    pointer.X = x;
    pointer.Y = y;
    ResolveHitTarget(pointer, layout);

    event.Target = pointer.HotTarget;
    event.TargetIndex = pointer.TargetIndex;

    // Only log when the hot target actually changes -- a live mouse delivers
    // hundreds of samples per second and would otherwise flood the serial log.
    if (pointer.HotTarget != prev_target || pointer.TargetIndex != prev_index) {
        event.Moved = true;
        Debug::Printf("[SHELLINPUT] Session %u pointer over %s[%u] at (%u,%u)\r\n",
                      pointer.SessionId, HitName(pointer.HotTarget),
                      pointer.TargetIndex, pointer.X, pointer.Y);
    }

    // Click = left button release edge (press followed by release).
    if (pointer.LeftButtonDown && !left_down) {
        event.Clicked = true;
        pointer.ClickDelivered = true;
        Debug::Printf("[SHELLINPUT] Session %u pointer click -> %s[%u]\r\n",
                      pointer.SessionId, HitName(pointer.HotTarget),
                      pointer.TargetIndex);
    }
    pointer.LeftButtonDown = left_down;

    return event;
}

} // namespace SHELLINPUT
