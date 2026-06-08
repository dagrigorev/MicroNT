#pragma once
// uxtheme.h -- MicroNT visual style/theme boundary.

#include "ntdef.h"

namespace UXTHEME {

struct Theme {
    const char* Name;
    u32 WallpaperTop;
    u32 WallpaperBottom;
    u32 TaskbarTop;
    u32 TaskbarBottom;
    u32 StartTop;
    u32 StartBottom;
    u32 WindowFrame;
    u32 WindowTitleTop;
    u32 WindowTitleBottom;
    u32 Accent;          // Fluent accent color (Win11 default #0078D4)
    bool DarkMode;       // dark taskbar / shell chrome
    bool HighResolutionMetrics;
};

void Init();
bool LoadDefaultTheme(Theme& theme);

} // namespace UXTHEME
