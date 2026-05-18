// compositor.cpp -- MicroNT Win32K/DWM bootstrap boundary.

#include "../include/win32k.h"
#include "../include/debug.h"
#include "../include/hal.h"

namespace WIN32K {

static bool s_initialized = false;

void Init() {
    s_initialized = true;
    Debug::Print("[WIN32K] Graphics subsystem initialized\r\n");
}

bool StartCsrss(SessionGraphics& graphics, u32 session_id) {
    if (!s_initialized) return false;
    graphics.SessionId = session_id;
    graphics.CsrssReady = true;
    graphics.DwmReady = false;
    Debug::Printf("[CSRSS] Session %u Win32 subsystem ready\r\n", session_id);
    return true;
}

bool StartDwm(SessionGraphics& graphics) {
    if (!graphics.CsrssReady) return false;
    graphics.DwmReady = true;
    Debug::Printf("[DWM] Session %u compositor started\r\n", graphics.SessionId);
    return true;
}

void PresentShellDesktop(SessionGraphics& graphics) {
    if (!graphics.DwmReady) return;
    Debug::Printf("[DWM] Session %u presenting shell desktop\r\n",
                  graphics.SessionId);
    VGA::StartDesktop();
    VGA::WriteWelcome();
}

} // namespace WIN32K
