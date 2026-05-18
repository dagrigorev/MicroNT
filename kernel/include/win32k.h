#pragma once
// win32k.h -- MicroNT graphical subsystem boundary.
//
// Windows keeps user sessions, the Win32 subsystem, window management, and
// composition separated. MicroNT starts with a small kernel-side boundary so
// session bootstrap no longer paints the framebuffer directly.

#include "ntdef.h"

namespace WIN32K {

struct SessionGraphics {
    u32  SessionId;
    bool CsrssReady;
    bool DwmReady;
};

void Init();
bool StartCsrss(SessionGraphics& graphics, u32 session_id);
bool StartDwm(SessionGraphics& graphics);
void PresentShellDesktop(SessionGraphics& graphics);

} // namespace WIN32K
