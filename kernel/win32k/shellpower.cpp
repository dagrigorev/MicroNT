// shellpower.cpp -- MicroNT shell power/session action model.

#include "../include/shellpower.h"
#include "../include/debug.h"

namespace SHELLPOWER {

static PowerAction MakeAction(PowerActionKind kind, const char* label,
                              bool enabled, bool confirm) {
    PowerAction action{};
    action.Kind = kind;
    action.Label = label;
    action.Enabled = enabled;
    action.RequiresConfirmation = confirm;
    return action;
}

void Init() {
    Debug::Print("[SHELLPOWER] Shell power model initialized\r\n");
}

bool BuildPowerState(PowerState& power,
                     const SHELLSTART::StartMenuState& menu) {
    if (!menu.Published || menu.SessionId == 0) return false;

    power = {};
    power.SessionId = menu.SessionId;
    power.FlyoutReady = true;

    power.Actions[power.ActionCount++] =
        MakeAction(PowerActionKind::Lock, "Lock", true, false);
    power.Actions[power.ActionCount++] =
        MakeAction(PowerActionKind::SignOut, "Sign out", true, true);
    power.Actions[power.ActionCount++] =
        MakeAction(PowerActionKind::Sleep, "Sleep", false, false);
    power.Actions[power.ActionCount++] =
        MakeAction(PowerActionKind::Restart, "Restart", true, true);
    power.Actions[power.ActionCount++] =
        MakeAction(PowerActionKind::ShutDown, "Shut down", true, true);

    power.DefaultActionIndex = 4;

    Debug::Printf("[SHELLPOWER] Session %u power actions built (%u actions)\r\n",
                  power.SessionId, power.ActionCount);
    return true;
}

bool PublishPowerState(PowerState& power) {
    if (!power.FlyoutReady || power.ActionCount == 0 ||
        power.DefaultActionIndex >= power.ActionCount) {
        return false;
    }

    power.Published = true;
    Debug::Printf("[SHELLPOWER] Session %u default power action '%s'\r\n",
                  power.SessionId,
                  power.Actions[power.DefaultActionIndex].Label);
    return true;
}

} // namespace SHELLPOWER
