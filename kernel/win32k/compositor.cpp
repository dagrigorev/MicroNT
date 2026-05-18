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

bool AttachSession(SessionGraphics& graphics, const CSRSS::Win32Session& session) {
    if (!s_initialized || !session.RuntimeReady) return false;
    graphics.SessionId = session.SessionId;
    graphics.Win32Attached = true;
    graphics.DwmReady = false;
    Debug::Printf("[WIN32K] Session %u attached to Win32 runtime\r\n",
                  session.SessionId);
    return true;
}

bool StartDwm(SessionGraphics& graphics) {
    if (!graphics.Win32Attached) return false;
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
