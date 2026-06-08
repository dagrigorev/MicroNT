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
    u32 X;
    u32 Y;
    HitTargetKind HotTarget;
    u32 TargetIndex;
    bool HitTestingReady;
    bool LeftButtonDown;
    bool ClickDelivered;
};

// Result of feeding one live mouse sample into the hit-test model.
struct PointerEvent {
    bool Moved;              // hot target or index changed since last sample
    bool Clicked;            // left button release edge (press -> release)
    HitTargetKind Target;    // target under the pointer at event time
    u32 TargetIndex;         // sub-index (icon/window/taskbar slot)
};

void Init();
bool AttachLayout(PointerState& pointer,
                  const INPUTHOST::InputDesktop& input,
                  const DESKTOPMODEL::DesktopLayout& layout);
bool PrimeDefaultHitTarget(PointerState& pointer,
                           const DESKTOPMODEL::DesktopLayout& layout);
bool MovePointer(PointerState& pointer,
                 const DESKTOPMODEL::DesktopLayout& layout,
                 u32 x, u32 y);
bool ClickPointer(PointerState& pointer,
                  const DESKTOPMODEL::DesktopLayout& layout,
                  u32 x, u32 y);

// Live-input path: feed one absolute sample from the PS/2 mouse.  Updates the
// pointer position, re-resolves the hot target quietly (logging only when the
// target changes), and reports a click on the left-button release edge.
PointerEvent ProcessPointer(PointerState& pointer,
                            const DESKTOPMODEL::DesktopLayout& layout,
                            u32 x, u32 y, bool left_down);

} // namespace SHELLINPUT
