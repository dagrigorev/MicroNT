// inputhost.cpp -- MicroNT interactive input routing boundary.

#include "../include/inputhost.h"
#include "../include/debug.h"

namespace INPUTHOST {

void Init() {
    Debug::Print("[INPUTHOST] Input host initialized\r\n");
}

bool AttachDesktop(InputDesktop& input, const WINSTA::Desktop& desktop) {
    if (desktop.SessionId == 0) return false;

    input.SessionId = desktop.SessionId;
    input.AttachedToDesktop = true;
    input.FocusedShellSurface = false;
    input.CursorVisible = false;

    Debug::Printf("[INPUTHOST] Session %u input desktop attached\r\n",
                  input.SessionId);
    return true;
}

bool FocusShellSurface(InputDesktop& input,
                       const SHELLHOST::ShellSurface& surface) {
    if (!input.AttachedToDesktop || surface.SessionId != input.SessionId ||
        !surface.BoundToExplorer) {
        return false;
    }

    input.FocusedShellSurface = true;
    Debug::Printf("[INPUTHOST] Session %u shell surface focused\r\n",
                  input.SessionId);
    return true;
}

bool ShowCursor(InputDesktop& input) {
    if (!input.FocusedShellSurface) return false;

    input.CursorVisible = true;
    Debug::Printf("[INPUTHOST] Session %u cursor visible\r\n", input.SessionId);
    return true;
}

} // namespace INPUTHOST
