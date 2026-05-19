#pragma once
// shellstart.h -- MicroNT Start menu model.

#include "desktopmodel.h"
#include "shellact.h"
#include "ntdef.h"

namespace SHELLSTART {

enum class StartItemKind : u32 {
    Pinned,
    Place,
    System,
    Command,
};

struct StartItem {
    StartItemKind Kind;
    const char* Label;
    const char* Target;
    bool Enabled;
};

struct StartMenuState {
    u32 SessionId;
    StartItem Items[16];
    u32 ItemCount;
    bool Open;
    bool Published;
};

void Init();
bool BuildStartMenu(StartMenuState& menu,
                    const DESKTOPMODEL::DesktopLayout& layout,
                    const SHELLACT::ActivationState& activation);
bool PublishStartMenu(StartMenuState& menu);

} // namespace SHELLSTART
