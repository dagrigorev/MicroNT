#pragma once
// shellhost.h -- MicroNT ShellExperienceHost boundary.

#include "explorer.h"
#include "ntdef.h"
#include "winsta.h"

namespace SHELLHOST {

struct ShellSurface {
    u32 SessionId;
    bool TaskbarReady;
    bool StartSurfaceReady;
    bool TrayReady;
    bool BoundToExplorer;
};

void Init();
bool CreateShellSurface(ShellSurface& surface, WINSTA::Desktop& desktop);
bool BindExplorer(ShellSurface& surface, const EXPLORER::Shell& shell);
bool ComposeDesktop(ShellSurface& surface);

} // namespace SHELLHOST
