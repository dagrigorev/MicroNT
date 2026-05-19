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
    bool HighResolutionMetrics;
};

void Init();
bool LoadDefaultTheme(Theme& theme);

} // namespace UXTHEME
