// uxtheme.cpp -- MicroNT visual style/theme boundary.

#include "../include/uxtheme.h"
#include "../include/debug.h"

namespace UXTHEME {

void Init() {
    Debug::Print("[UXTHEME] Theme service initialized\r\n");
}

bool LoadDefaultTheme(Theme& theme) {
    // Windows 11 "Fluent" look: dark taskbar, accent blue, Bloom wallpaper,
    // light window surfaces.
    theme.Name = "MicroNT 11";
    theme.WallpaperTop = 0x0A2A5E;       // deep blue (top of Bloom)
    theme.WallpaperBottom = 0x0C1730;    // near-black blue (bottom)
    theme.TaskbarTop = 0x2C2C2C;         // dark flat taskbar
    theme.TaskbarBottom = 0x1E1E1E;
    theme.StartTop = 0x2C2C2C;
    theme.StartBottom = 0x1E1E1E;
    theme.WindowFrame = 0xF3F3F3;        // light Mica surface
    theme.WindowTitleTop = 0x0078D4;     // flat accent title bar
    theme.WindowTitleBottom = 0x0067B8;
    theme.Accent = 0x0078D4;             // Win11 default accent
    theme.DarkMode = true;
    theme.HighResolutionMetrics = true;

    Debug::Printf("[UXTHEME] Theme '%s' loaded\r\n", theme.Name);
    return true;
}

} // namespace UXTHEME
