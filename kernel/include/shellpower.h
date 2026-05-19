#pragma once
// shellpower.h -- MicroNT shell power/session action model.

#include "shellstart.h"
#include "ntdef.h"

namespace SHELLPOWER {

enum class PowerActionKind : u32 {
    Lock,
    SignOut,
    Sleep,
    Restart,
    ShutDown,
};

struct PowerAction {
    PowerActionKind Kind;
    const char* Label;
    bool Enabled;
    bool RequiresConfirmation;
};

struct PowerState {
    u32 SessionId;
    PowerAction Actions[8];
    u32 ActionCount;
    u32 DefaultActionIndex;
    bool FlyoutReady;
    bool Published;
};

void Init();
bool BuildPowerState(PowerState& power,
                     const SHELLSTART::StartMenuState& menu);
bool PublishPowerState(PowerState& power);

} // namespace SHELLPOWER
