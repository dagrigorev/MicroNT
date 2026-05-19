#pragma once
// inputhost.h -- MicroNT interactive input routing boundary.

#include "ntdef.h"
#include "shellhost.h"
#include "winsta.h"

namespace INPUTHOST {

struct InputDesktop {
    u32 SessionId;
    bool AttachedToDesktop;
    bool FocusedShellSurface;
    bool CursorVisible;
};

void Init();
bool AttachDesktop(InputDesktop& input, const WINSTA::Desktop& desktop);
bool FocusShellSurface(InputDesktop& input, const SHELLHOST::ShellSurface& surface);
bool ShowCursor(InputDesktop& input);

} // namespace INPUTHOST
