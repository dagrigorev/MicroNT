// shelltaskbar.cpp -- MicroNT taskbar button model.

#include "../include/shelltaskbar.h"
#include "../include/debug.h"

namespace SHELLTASKBAR {

void Init() {
    Debug::Print("[SHELLTASKBAR] Shell taskbar model initialized\r\n");
}

bool BuildTaskbar(TaskbarState& taskbar,
                  const DESKTOPMODEL::DesktopLayout& layout,
                  const SHELLACT::ActivationState& activation) {
    if (!layout.Ready || !activation.TaskbarReady ||
        layout.SessionId != activation.SessionId) {
        return false;
    }

    taskbar = {};
    taskbar.SessionId = layout.SessionId;

    for (u32 i = 0; i < layout.WindowCount && taskbar.ButtonCount < 8; ++i) {
        TaskButton& button = taskbar.Buttons[taskbar.ButtonCount++];
        button.Title = layout.Windows[i].Title;
        button.WindowIndex = i;
        button.Active = i == activation.ActiveWindowIndex;
        button.Visible = true;
    }

    if (taskbar.ButtonCount < 8) {
        TaskButton& shell = taskbar.Buttons[taskbar.ButtonCount++];
        shell.Title = "MicroNT Shell";
        shell.WindowIndex = 0xFFFFFFFFu;
        shell.Active = false;
        shell.Visible = true;
    }

    Debug::Printf("[SHELLTASKBAR] Session %u taskbar built (%u buttons)\r\n",
                  taskbar.SessionId, taskbar.ButtonCount);
    return true;
}

bool PublishTaskbar(TaskbarState& taskbar) {
    if (taskbar.SessionId == 0 || taskbar.ButtonCount == 0) return false;

    taskbar.Published = true;
    Debug::Printf("[SHELLTASKBAR] Session %u taskbar published\r\n",
                  taskbar.SessionId);
    return true;
}

} // namespace SHELLTASKBAR
