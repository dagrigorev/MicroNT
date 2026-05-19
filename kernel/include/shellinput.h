#pragma once
// shellinput.h -- MicroNT shell hit-test/input model.

#include "desktopmodel.h"
#include "inputhost.h"
#include "ntdef.h"

namespace SHELLINPUT {

enum class HitTargetKind : u32 {
    None,
    Desktop,
    DesktopIcon,
    ShellWindow,
    StartButton,
    StartMenu,
    Taskbar,
    Tray,
};

struct PointerState {
    u32 SessionId;
    HitTargetKind HotTarget;
    u32 TargetIndex;
    bool HitTestingReady;
};

void Init();
bool AttachLayout(PointerState& pointer,
                  const INPUTHOST::InputDesktop& input,
                  const DESKTOPMODEL::DesktopLayout& layout);
bool PrimeDefaultHitTarget(PointerState& pointer,
                           const DESKTOPMODEL::DesktopLayout& layout);

} // namespace SHELLINPUT
