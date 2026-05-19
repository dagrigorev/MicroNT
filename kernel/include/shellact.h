#pragma once
// shellact.h -- MicroNT shell activation/session state model.

#include "desktopmodel.h"
#include "shellinput.h"
#include "ntdef.h"

namespace SHELLACT {

struct ActivationState {
    u32 SessionId;
    u32 ActiveWindowIndex;
    bool StartMenuOpen;
    bool TaskbarReady;
    bool ActivationReady;
};

void Init();
bool CreateActivationState(ActivationState& state,
                           const DESKTOPMODEL::DesktopLayout& layout);
bool ApplyPointerTarget(ActivationState& state,
                        const SHELLINPUT::PointerState& pointer,
                        const DESKTOPMODEL::DesktopLayout& layout);
bool PublishTaskbarState(ActivationState& state,
                         const DESKTOPMODEL::DesktopLayout& layout);

} // namespace SHELLACT
