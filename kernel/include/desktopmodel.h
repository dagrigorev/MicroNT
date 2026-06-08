#pragma once
// desktopmodel.h -- MicroNT shell desktop presentation model.

#include "ntdef.h"
#include "windowmgr.h"

namespace DESKTOPMODEL {

enum class IconKind : u32 {
    Computer,
    Folder,
    Network,
    Media,
    Control,
    Recycle,
};

struct DesktopIcon {
    IconKind Kind;
    const char* Label;
};

struct ShellWindow {
    const char* Title;
    const char* Status;
    u32 X;
    u32 Y;
    u32 Width;
    u32 Height;
    bool Toolbar;
};

struct DesktopLayout {
    u32 SessionId;
    const char* BrandName;
    const char* BrandTag;
    u32 ScreenW;
    u32 ScreenH;
    DesktopIcon Icons[8];
    u32 IconCount;
    ShellWindow Windows[8];
    u32 WindowCount;
    bool StartMenuOpen;
    bool Ready;
};

// Windows 11 centered-taskbar geometry, shared by the renderer (vga.cpp) and
// the pointer hit-test (shellinput.cpp) so the two never disagree.  The Start
// button and app buttons form a horizontally centered cluster; the tray is
// pinned to the right.
struct TaskbarMetrics {
    u32 Height;
    u32 Y;            // taskbar top edge
    u32 StartX;       // Start (Windows logo) button
    u32 StartW;
    u32 FirstButtonX; // first app button in the centered cluster
    u32 ButtonW;
    u32 ButtonGap;
    u32 ButtonCount;
    u32 TrayX;        // right-aligned notification area
    u32 TrayW;
};

void Init();
bool BuildXpRedesignLayout(DesktopLayout& layout,
                           const WINDOWMGR::DesktopScene& scene);
TaskbarMetrics ComputeTaskbar(u32 screen_w, u32 screen_h, u32 button_count);

} // namespace DESKTOPMODEL
