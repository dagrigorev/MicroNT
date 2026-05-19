// uxtheme.cpp -- MicroNT visual style/theme boundary.

#include "../include/uxtheme.h"
#include "../include/debug.h"

namespace UXTHEME {

void Init() {
    Debug::Print("[UXTHEME] Theme service initialized\r\n");
}

bool LoadDefaultTheme(Theme& theme) {
    theme.Name = "MicroNT Luna";
    theme.WallpaperTop = 0x5DB9FF;
    theme.WallpaperBottom = 0x9BD6FF;
    theme.TaskbarTop = 0x2D7FF0;
    theme.TaskbarBottom = 0x0E3FA5;
    theme.StartTop = 0x7FD34E;
    theme.StartBottom = 0x258B1F;
    theme.WindowFrame = 0xECE9D8;
    theme.WindowTitleTop = 0x2F7DFF;
    theme.WindowTitleBottom = 0x0A3BB7;
    theme.HighResolutionMetrics = true;

    Debug::Printf("[UXTHEME] Theme '%s' loaded\r\n", theme.Name);
    return true;
}

} // namespace UXTHEME
