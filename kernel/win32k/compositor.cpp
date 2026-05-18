// compositor.cpp -- MicroNT Win32K/DWM bootstrap boundary.

#include "../include/win32k.h"
#include "../include/debug.h"

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
    Debug::Printf("[WIN32K] Session %u attached to Win32 runtime\r\n",
                  session.SessionId);
    return true;
}

} // namespace WIN32K
