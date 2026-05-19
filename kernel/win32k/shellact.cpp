// shellact.cpp -- MicroNT shell activation/session state model.

#include "../include/shellact.h"
#include "../include/debug.h"

namespace SHELLACT {

void Init() {
    Debug::Print("[SHELLACT] Shell activation model initialized\r\n");
}

bool CreateActivationState(ActivationState& state,
                           const DESKTOPMODEL::DesktopLayout& layout) {
    if (!layout.Ready || layout.WindowCount == 0) return false;

    state.SessionId = layout.SessionId;
    state.ActiveWindowIndex = 0;
    state.StartMenuOpen = layout.StartMenuOpen;
    state.TaskbarReady = false;
    state.ActivationReady = true;

    Debug::Printf("[SHELLACT] Session %u activation state ready (start=%s)\r\n",
                  state.SessionId, state.StartMenuOpen ? "open" : "closed");
    return true;
}

bool ApplyPointerTarget(ActivationState& state,
                        const SHELLINPUT::PointerState& pointer,
                        const DESKTOPMODEL::DesktopLayout& layout) {
    if (!state.ActivationReady || !pointer.HitTestingReady ||
        state.SessionId != pointer.SessionId ||
        state.SessionId != layout.SessionId) {
        return false;
    }

    if (pointer.HotTarget == SHELLINPUT::HitTargetKind::ShellWindow &&
        pointer.TargetIndex < layout.WindowCount) {
        state.ActiveWindowIndex = pointer.TargetIndex;
        state.StartMenuOpen = false;
    } else if (pointer.HotTarget == SHELLINPUT::HitTargetKind::StartButton ||
               pointer.HotTarget == SHELLINPUT::HitTargetKind::StartMenu) {
        state.StartMenuOpen = true;
    }

    Debug::Printf("[SHELLACT] Session %u active window '%s'\r\n",
                  state.SessionId, layout.Windows[state.ActiveWindowIndex].Title);
    return true;
}

bool PublishTaskbarState(ActivationState& state,
                         const DESKTOPMODEL::DesktopLayout& layout) {
    if (!state.ActivationReady || state.SessionId != layout.SessionId) {
        return false;
    }

    state.TaskbarReady = true;
    Debug::Printf("[SHELLACT] Session %u taskbar activation published (%u buttons)\r\n",
                  state.SessionId, layout.WindowCount + 1);
    return true;
}

} // namespace SHELLACT
