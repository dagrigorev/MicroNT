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
    DesktopIcon Icons[8];
    u32 IconCount;
    ShellWindow Windows[8];
    u32 WindowCount;
    bool StartMenuOpen;
    bool Ready;
};

void Init();
bool BuildXpRedesignLayout(DesktopLayout& layout,
                           const WINDOWMGR::DesktopScene& scene);

} // namespace DESKTOPMODEL
