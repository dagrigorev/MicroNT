// uxtheme.cpp -- MicroNT visual style/theme boundary.

#include "../include/uxtheme.h"
#include "../include/debug.h"

namespace UXTHEME {

void Init() {
    Debug::Print("[UXTHEME] Theme service initialized\r\n");
}

bool LoadDefaultTheme(Theme& theme) {
    // XP x 11 fusion: Windows 11 layout (centered taskbar, Mica window
    // bodies, modern controls, Fluent icons) wearing Windows XP "Luna"
    // flavor -- a Bliss-style sky/hills wallpaper, glossy blue taskbar and
    // title bars, and a green glossy Start.
    theme.Name = "MicroNT XP.11 Fusion";
    theme.WallpaperTop = 0x4F9BEE;       // Bliss sky (top)
    theme.WallpaperBottom = 0xCDEBFF;    // pale horizon
    theme.TaskbarTop = 0x4C93F0;         // Luna glossy blue
    theme.TaskbarBottom = 0x16409E;
    theme.StartTop = 0x8FD15A;           // Luna green Start
    theme.StartBottom = 0x2C8B1E;
    theme.WindowFrame = 0xF3F3F3;        // Win11 light Mica body
    theme.WindowTitleTop = 0x4090F4;     // Luna glossy title bar
    theme.WindowTitleBottom = 0x1B5BD0;
    theme.Accent = 0x0078D4;             // shared accent
    theme.DarkMode = false;
    theme.HighResolutionMetrics = true;

    Debug::Printf("[UXTHEME] Theme '%s' loaded\r\n", theme.Name);
    return true;
}

} // namespace UXTHEME
