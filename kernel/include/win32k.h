#pragma once
// win32k.h -- MicroNT graphical subsystem boundary.
//
// Windows keeps user sessions, the Win32 subsystem, window management, and
// composition separated. MicroNT starts with a small kernel-side boundary so
// session bootstrap no longer paints the framebuffer directly.

#include "ntdef.h"
#include "csrss.h"

namespace WIN32K {

struct SessionGraphics {
    u32  SessionId;
    bool Win32Attached;
    bool DwmReady;
};

void Init();
bool AttachSession(SessionGraphics& graphics, const CSRSS::Win32Session& session);
bool StartDwm(SessionGraphics& graphics);
void PresentShellDesktop(SessionGraphics& graphics);

} // namespace WIN32K
