// shella11y.cpp -- MicroNT shell accessibility/focus cues model.

#include "../include/shella11y.h"
#include "../include/debug.h"

namespace SHELLA11Y {

void Init() {
    Debug::Print("[SHELLA11Y] Shell accessibility initialized\r\n");
}

bool CreateAccessibilityState(AccessibilityState& state,
                              const SHELLACT::ActivationState& activation) {
    if (!activation.ActivationReady) return false;

    state.SessionId = activation.SessionId;
    state.FocusCuesVisible = true;
    state.HighContrast = false;
    state.NarratorHooksReady = true;
    state.Ready = true;

    Debug::Printf("[SHELLA11Y] Session %u accessibility state ready\r\n",
                  state.SessionId);
    return true;
}

bool PublishFocusCues(AccessibilityState& state,
                      const SHELLACT::ActivationState& activation) {
    if (!state.Ready || state.SessionId != activation.SessionId) return false;

    Debug::Printf("[SHELLA11Y] Session %u focus cues published for window %u\r\n",
                  state.SessionId, activation.ActiveWindowIndex);
    return true;
}

} // namespace SHELLA11Y
