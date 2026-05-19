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
                         const WINDOWMGR::DesktopScene& scene,
                         const DISPLAYCFG::DisplayTarget& target,
                         const UXTHEME::Theme& theme) {
    if (!compositor.Running) return;
    if (!scene.ZOrderReady || scene.SessionId != compositor.SessionId) return;
    if (!target.MetricsReady || target.SessionId != compositor.SessionId) return;
    if (!WINSTA::SwitchDesktop(desktop)) return;
    Debug::Printf("[DWM] Session %u composing %u top-level windows at %ux%u with theme '%s'\r\n",
                  compositor.SessionId, scene.WindowCount,
                  target.Mode.Width, target.Mode.Height, theme.Name);
    VGA::StartDesktop(theme);
    VGA::WriteWelcome();
    compositor.DesktopPresented = true;
}

} // namespace DWM
