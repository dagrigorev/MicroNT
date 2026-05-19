// winsta.cpp -- MicroNT window station and desktop objects.

#include "../include/winsta.h"
#include "../include/debug.h"

namespace WINSTA {

void Init() {
    Debug::Print("[WINSTA] Window station subsystem initialized\r\n");
}

bool CreateInteractiveWindowStation(WindowStation& station, u32 session_id) {
    station.SessionId = session_id;
    station.Ready = true;
    Debug::Printf("[WINSTA] Session %u WinSta0 ready\r\n", session_id);
    return true;
}

bool CreateDesktop(Desktop& desktop, const WindowStation& station,
                   DesktopKind kind, const char* name) {
    if (!station.Ready || !name) return false;
    desktop.SessionId = station.SessionId;
    desktop.Kind = kind;
    desktop.Name = name;
    desktop.Active = false;
    Debug::Printf("[WINSTA] Session %u desktop '%s' created\r\n",
                  desktop.SessionId, desktop.Name);
    return true;
}

bool SwitchDesktop(Desktop& desktop) {
    if (!desktop.Name) return false;
    desktop.Active = true;
    Debug::Printf("[WINSTA] Session %u desktop '%s' active\r\n",
                  desktop.SessionId, desktop.Name);
    return true;
}

} // namespace WINSTA
