// shellstart.cpp -- MicroNT Start menu model.

#include "../include/shellstart.h"
#include "../include/debug.h"

namespace SHELLSTART {

static StartItem MakeItem(StartItemKind kind, const char* label,
                          const char* target) {
    StartItem item{};
    item.Kind = kind;
    item.Label = label;
    item.Target = target;
    item.Enabled = true;
    return item;
}

void Init() {
    Debug::Print("[SHELLSTART] Start menu model initialized\r\n");
}

bool BuildStartMenu(StartMenuState& menu,
                    const DESKTOPMODEL::DesktopLayout& layout,
                    const SHELLACT::ActivationState& activation) {
    if (!layout.Ready || !activation.ActivationReady ||
        layout.SessionId != activation.SessionId) {
        return false;
    }

    menu = {};
    menu.SessionId = layout.SessionId;
    menu.Open = activation.StartMenuOpen;

    menu.Items[menu.ItemCount++] =
        MakeItem(StartItemKind::Pinned, "Internet", "micront-browser.exe");
    menu.Items[menu.ItemCount++] =
        MakeItem(StartItemKind::Pinned, "E-mail", "mail.exe");
    menu.Items[menu.ItemCount++] =
        MakeItem(StartItemKind::Pinned, "Media Player", "media.exe");
    menu.Items[menu.ItemCount++] =
        MakeItem(StartItemKind::Place, "Documents", "shell:documents");
    menu.Items[menu.ItemCount++] =
        MakeItem(StartItemKind::Place, "My Computer", "shell:computer");
    menu.Items[menu.ItemCount++] =
        MakeItem(StartItemKind::System, "Control Panel", "shell:control");
    menu.Items[menu.ItemCount++] =
        MakeItem(StartItemKind::Command, "Search", "shell:search");
    menu.Items[menu.ItemCount++] =
        MakeItem(StartItemKind::Command, "Run...", "shell:run");

    Debug::Printf("[SHELLSTART] Session %u start menu built (%u items, %s)\r\n",
                  menu.SessionId, menu.ItemCount,
                  menu.Open ? "open" : "closed");
    return true;
}

bool PublishStartMenu(StartMenuState& menu) {
    if (menu.SessionId == 0 || menu.ItemCount == 0) return false;

    menu.Published = true;
    Debug::Printf("[SHELLSTART] Session %u start menu published\r\n",
                  menu.SessionId);
    return true;
}

} // namespace SHELLSTART
