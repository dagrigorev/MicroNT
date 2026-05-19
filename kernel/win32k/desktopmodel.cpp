// desktopmodel.cpp -- MicroNT shell desktop presentation model.

#include "../include/desktopmodel.h"
#include "../include/debug.h"

namespace DESKTOPMODEL {

static DesktopIcon MakeIcon(IconKind kind, const char* label) {
    DesktopIcon icon{};
    icon.Kind = kind;
    icon.Label = label;
    return icon;
}

static ShellWindow MakeWindow(const char* title, const char* status,
                              u32 x, u32 y, u32 w, u32 h, bool toolbar) {
    ShellWindow window{};
    window.Title = title;
    window.Status = status;
    window.X = x;
    window.Y = y;
    window.Width = w;
    window.Height = h;
    window.Toolbar = toolbar;
    return window;
}

void Init() {
    Debug::Print("[DESKTOPMODEL] Desktop model initialized\r\n");
}

bool BuildXpRedesignLayout(DesktopLayout& layout,
                           const WINDOWMGR::DesktopScene& scene) {
    if (!scene.ZOrderReady || scene.SessionId == 0) return false;

    layout = {};
    layout.SessionId = scene.SessionId;
    layout.BrandName = "MicroNT";
    layout.BrandTag = "Simply Fast. Reliably Yours.";
    layout.StartMenuOpen = true;
    layout.Ready = true;

    layout.Icons[layout.IconCount++] = MakeIcon(IconKind::Computer, "Computer");
    layout.Icons[layout.IconCount++] = MakeIcon(IconKind::Folder, "Documents");
    layout.Icons[layout.IconCount++] = MakeIcon(IconKind::Network, "Network");
    layout.Icons[layout.IconCount++] = MakeIcon(IconKind::Media, "Media");
    layout.Icons[layout.IconCount++] = MakeIcon(IconKind::Control, "Control");
    layout.Icons[layout.IconCount++] = MakeIcon(IconKind::Recycle, "Recycle");

    layout.Windows[layout.WindowCount++] =
        MakeWindow("My Computer", "MICRONT-SYSTEM  WORKGROUP",
                   228, 38, 665, 430, true);
    layout.Windows[layout.WindowCount++] =
        MakeWindow("Documents", "10 items",
                   1015, 58, 620, 365, true);
    layout.Windows[layout.WindowCount++] =
        MakeWindow("Network", "Signal: excellent",
                   645, 420, 575, 290, true);
    layout.Windows[layout.WindowCount++] =
        MakeWindow("Control Panel", "MicroNT Control Center",
                   1238, 420, 600, 360, false);
    layout.Windows[layout.WindowCount++] =
        MakeWindow("Media Player", "02:37 / 05:45",
                   430, 708, 720, 260, true);

    Debug::Printf("[DESKTOPMODEL] Session %u XP redesign layout ready (%u icons, %u windows)\r\n",
                  layout.SessionId, layout.IconCount, layout.WindowCount);
    return true;
}

} // namespace DESKTOPMODEL
