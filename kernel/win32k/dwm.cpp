// dwm.cpp -- MicroNT Desktop Window Manager boundary.

#include "../include/dwm.h"
#include "../include/debug.h"
#include "../include/hal.h"

namespace DWM {

void Init() {
    Debug::Print("[DWM] Desktop Window Manager initialized\r\n");
}

bool Start(Compositor& compositor, const WIN32K::SessionGraphics& graphics) {
    if (!graphics.Win32Attached) return false;
    compositor.SessionId = graphics.SessionId;
    compositor.Running = true;
    compositor.DesktopPresented = false;
    Debug::Printf("[DWM] Session %u compositor started\r\n",
                  compositor.SessionId);
    return true;
}

void PresentShellDesktop(Compositor& compositor, WINSTA::Desktop& desktop,
                         const SHELLHOST::ShellSurface& surface) {
    if (!compositor.Running) return;
    if (!surface.BoundToExplorer || surface.SessionId != compositor.SessionId) return;
    if (!WINSTA::SwitchDesktop(desktop)) return;
    Debug::Printf("[DWM] Session %u presenting ShellExperienceHost surface\r\n",
                  compositor.SessionId);
    VGA::StartDesktop();
    VGA::WriteWelcome();
    compositor.DesktopPresented = true;
}

} // namespace DWM
