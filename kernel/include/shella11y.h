#pragma once
// shella11y.h -- MicroNT shell accessibility/focus cues model.

#include "shellact.h"
#include "ntdef.h"

namespace SHELLA11Y {

struct AccessibilityState {
    u32 SessionId;
    bool FocusCuesVisible;
    bool HighContrast;
    bool NarratorHooksReady;
    bool Ready;
};

void Init();
bool CreateAccessibilityState(AccessibilityState& state,
                              const SHELLACT::ActivationState& activation);
bool PublishFocusCues(AccessibilityState& state,
                      const SHELLACT::ActivationState& activation);

} // namespace SHELLA11Y
