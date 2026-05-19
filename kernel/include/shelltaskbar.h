#pragma once
// shelltaskbar.h -- MicroNT taskbar button model.

#include "desktopmodel.h"
#include "shellact.h"
#include "ntdef.h"

namespace SHELLTASKBAR {

struct TaskButton {
    const char* Title;
    u32 WindowIndex;
    bool Active;
    bool Visible;
};

struct TaskbarState {
    u32 SessionId;
    TaskButton Buttons[8];
    u32 ButtonCount;
    bool Published;
};

void Init();
bool BuildTaskbar(TaskbarState& taskbar,
                  const DESKTOPMODEL::DesktopLayout& layout,
                  const SHELLACT::ActivationState& activation);
bool PublishTaskbar(TaskbarState& taskbar);

} // namespace SHELLTASKBAR
