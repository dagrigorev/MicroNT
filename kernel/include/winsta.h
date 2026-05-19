#pragma once
// winsta.h -- MicroNT window station and desktop objects.

#include "ntdef.h"

namespace WINSTA {

enum class DesktopKind : u32 {
    SecureLogon = 0,
    Shell,
};

struct Desktop {
    u32         SessionId;
    DesktopKind Kind;
    const char* Name;
    bool        Active;
};

struct WindowStation {
    u32  SessionId;
    bool Ready;
};

void Init();
bool CreateInteractiveWindowStation(WindowStation& station, u32 session_id);
bool CreateDesktop(Desktop& desktop, const WindowStation& station,
                   DesktopKind kind, const char* name);
bool SwitchDesktop(Desktop& desktop);

} // namespace WINSTA
