#pragma once
// shelltray.h -- MicroNT notification area / system tray model.

#include "shellact.h"
#include "ntdef.h"

namespace SHELLTRAY {

enum class TrayIconKind : u32 {
    Network,
    Volume,
    Status,
};

struct TrayIcon {
    TrayIconKind Kind;
    const char* Label;
    bool Visible;
};

struct TrayState {
    u32 SessionId;
    TrayIcon Icons[4];
    u32 IconCount;
    const char* ClockText;
    bool Published;
};

void Init();
bool CreateTrayState(TrayState& tray,
                     const SHELLACT::ActivationState& activation);
bool PublishTrayState(TrayState& tray);

} // namespace SHELLTRAY
