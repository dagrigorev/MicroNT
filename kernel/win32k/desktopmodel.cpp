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
    layout.ScreenW = 1920;   // refined from the display target in the session mgr
    layout.ScreenH = 1080;
    layout.StartMenuOpen = false;   // Windows 11 boots with Start closed
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

TaskbarMetrics ComputeTaskbar(u32 screen_w, u32 screen_h, u32 button_count) {
    if (screen_w == 0) screen_w = 1920;
    if (screen_h == 0) screen_h = 1080;

    TaskbarMetrics m{};
    m.Height = screen_h >= 720 ? 48 : 40;
    m.Y = screen_h - m.Height;
    m.ButtonW = 44;
    m.ButtonGap = 6;
    m.ButtonCount = button_count;

    // Centered cluster = Start button + one button per running app.
    u32 items = button_count + 1;
    u32 slot = m.ButtonW + m.ButtonGap;
    u32 cluster_w = items * m.ButtonW + (items - 1) * m.ButtonGap;
    u32 cluster_x = screen_w > cluster_w ? (screen_w - cluster_w) / 2 : 0;

    m.StartX = cluster_x;
    m.StartW = m.ButtonW;
    m.FirstButtonX = cluster_x + slot;

    m.TrayW = screen_w >= 640 ? 200 : 120;
    m.TrayX = screen_w > m.TrayW ? screen_w - m.TrayW : 0;
    return m;
}

} // namespace DESKTOPMODEL
