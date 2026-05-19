#pragma once
// dwm.h -- MicroNT Desktop Window Manager boundary.

#include "ntdef.h"
#include "shellhost.h"
#include "winsta.h"
#include "win32k.h"

namespace DWM {

struct Compositor {
    u32  SessionId;
    bool Running;
    bool DesktopPresented;
};

void Init();
bool Start(Compositor& compositor, const WIN32K::SessionGraphics& graphics);
void PresentShellDesktop(Compositor& compositor, WINSTA::Desktop& desktop,
                         const SHELLHOST::ShellSurface& surface);

} // namespace DWM
