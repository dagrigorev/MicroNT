// shelltray.cpp -- MicroNT notification area / system tray model.

#include "../include/shelltray.h"
#include "../include/debug.h"

namespace SHELLTRAY {

static TrayIcon MakeIcon(TrayIconKind kind, const char* label) {
    TrayIcon icon{};
    icon.Kind = kind;
    icon.Label = label;
    icon.Visible = true;
    return icon;
}

void Init() {
    Debug::Print("[SHELLTRAY] Shell tray initialized\r\n");
}

bool CreateTrayState(TrayState& tray,
                     const SHELLACT::ActivationState& activation) {
    if (!activation.TaskbarReady) return false;

    tray = {};
    tray.SessionId = activation.SessionId;
    tray.ClockText = "00:00";
    tray.Icons[tray.IconCount++] = MakeIcon(TrayIconKind::Status, "OK");
    tray.Icons[tray.IconCount++] = MakeIcon(TrayIconKind::Network, "NET");
    tray.Icons[tray.IconCount++] = MakeIcon(TrayIconKind::Volume, "VOL");

    Debug::Printf("[SHELLTRAY] Session %u tray state ready (%u icons)\r\n",
                  tray.SessionId, tray.IconCount);
    return true;
}

bool PublishTrayState(TrayState& tray) {
    if (tray.SessionId == 0 || tray.IconCount == 0) return false;

    tray.Published = true;
    Debug::Printf("[SHELLTRAY] Session %u notification area published (%s)\r\n",
                  tray.SessionId, tray.ClockText);
    return true;
}

} // namespace SHELLTRAY
