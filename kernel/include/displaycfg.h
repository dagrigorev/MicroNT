#pragma once
// displaycfg.h -- MicroNT display configuration boundary.

#include "ntdef.h"

namespace DISPLAYCFG {

struct DisplayMode {
    u32 Width;
    u32 Height;
    u32 Stride;
    u32 Format;
    bool FullHd;
};

struct DisplayTarget {
    u32 SessionId;
    DisplayMode Mode;
    bool Attached;
    bool MetricsReady;
};

void Init();
bool QueryPrimaryMode(DisplayMode& mode);
bool AttachSessionTarget(DisplayTarget& target, u32 session_id,
                         const DisplayMode& mode);
bool ApplyDesktopMetrics(DisplayTarget& target);

} // namespace DISPLAYCFG
