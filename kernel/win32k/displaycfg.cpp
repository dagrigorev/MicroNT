// displaycfg.cpp -- MicroNT display configuration boundary.

#include "../include/displaycfg.h"
#include "../include/debug.h"
#include "../include/hal.h"

namespace DISPLAYCFG {

void Init() {
    Debug::Print("[DISPLAYCFG] Display configuration initialized\r\n");
}

bool QueryPrimaryMode(DisplayMode& mode) {
    VGA::FramebufferInfo fb{};
    if (!VGA::GetFramebufferInfo(fb)) return false;

    mode.Width = fb.Width;
    mode.Height = fb.Height;
    mode.Stride = fb.Stride;
    mode.Format = fb.Format;
    mode.FullHd = fb.Width == 1920 && fb.Height == 1080;

    Debug::Printf("[DISPLAYCFG] Primary target %ux%u stride=%u format=%u\r\n",
                  mode.Width, mode.Height, mode.Stride, mode.Format);
    if (mode.FullHd) {
        Debug::Print("[DISPLAYCFG] FullHD desktop target confirmed\r\n");
    } else {
        Debug::Print("[DISPLAYCFG] Using best available desktop target\r\n");
    }
    return true;
}

bool AttachSessionTarget(DisplayTarget& target, u32 session_id,
                         const DisplayMode& mode) {
    if (session_id == 0 || mode.Width == 0 || mode.Height == 0) return false;

    target.SessionId = session_id;
    target.Mode = mode;
    target.Attached = true;
    target.MetricsReady = false;

    Debug::Printf("[DISPLAYCFG] Session %u display target attached\r\n",
                  target.SessionId);
    return true;
}

bool ApplyDesktopMetrics(DisplayTarget& target) {
    if (!target.Attached) return false;

    target.MetricsReady = true;
    Debug::Printf("[DISPLAYCFG] Session %u desktop metrics %ux%u ready\r\n",
                  target.SessionId, target.Mode.Width, target.Mode.Height);
    return true;
}

} // namespace DISPLAYCFG
