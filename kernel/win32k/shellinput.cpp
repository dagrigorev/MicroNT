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
    pointer.HotTarget = HitTargetKind::None;
    pointer.TargetIndex = 0;
    pointer.HitTestingReady = true;

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
    Debug::Printf("[SHELLINPUT] Session %u default target=%s[%u]\r\n",
                  pointer.SessionId, HitName(pointer.HotTarget),
                  pointer.TargetIndex);
    return true;
}

} // namespace SHELLINPUT
