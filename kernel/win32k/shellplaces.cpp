// shellplaces.cpp -- MicroNT shell namespace/place model.

#include "../include/shellplaces.h"
#include "../include/debug.h"

namespace SHELLPLACES {

static bool StringEquals(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

static const char* TargetForKind(PlaceKind kind) {
    switch (kind) {
    case PlaceKind::Documents: return "shell:documents";
    case PlaceKind::Computer: return "shell:computer";
    case PlaceKind::ControlPanel: return "shell:control";
    case PlaceKind::Network: return "shell:network";
    case PlaceKind::Media: return "shell:media";
    case PlaceKind::RecycleBin: return "shell:recycle";
    }
    return "shell:unknown";
}

static PlaceKind KindFromIcon(DESKTOPMODEL::IconKind kind) {
    switch (kind) {
    case DESKTOPMODEL::IconKind::Computer: return PlaceKind::Computer;
    case DESKTOPMODEL::IconKind::Folder: return PlaceKind::Documents;
    case DESKTOPMODEL::IconKind::Network: return PlaceKind::Network;
    case DESKTOPMODEL::IconKind::Media: return PlaceKind::Media;
    case DESKTOPMODEL::IconKind::Control: return PlaceKind::ControlPanel;
    case DESKTOPMODEL::IconKind::Recycle: return PlaceKind::RecycleBin;
    }
    return PlaceKind::Computer;
}

static bool TryKindFromTarget(const char* target, PlaceKind& kind) {
    if (StringEquals(target, "shell:documents")) {
        kind = PlaceKind::Documents;
    } else if (StringEquals(target, "shell:computer")) {
        kind = PlaceKind::Computer;
    } else if (StringEquals(target, "shell:control")) {
        kind = PlaceKind::ControlPanel;
    } else {
        return false;
    }
    return true;
}

static ShellPlace* FindPlace(PlaceState& places, PlaceKind kind) {
    for (u32 i = 0; i < places.PlaceCount; ++i) {
        if (places.Places[i].Kind == kind) return &places.Places[i];
    }
    return nullptr;
}

static ShellPlace* AddOrGetPlace(PlaceState& places, PlaceKind kind,
                                 const char* label) {
    ShellPlace* existing = FindPlace(places, kind);
    if (existing) return existing;
    if (places.PlaceCount >= 12) return nullptr;

    ShellPlace& place = places.Places[places.PlaceCount++];
    place.Kind = kind;
    place.Label = label;
    place.Target = TargetForKind(kind);
    place.Enabled = true;
    return &place;
}

void Init() {
    Debug::Print("[SHELLPLACES] Shell namespace model initialized\r\n");
}

bool BuildPlaceState(PlaceState& places,
                     const DESKTOPMODEL::DesktopLayout& layout,
                     const SHELLSTART::StartMenuState& menu) {
    if (!layout.Ready || !menu.Published ||
        layout.SessionId != menu.SessionId) {
        return false;
    }

    places = {};
    places.SessionId = layout.SessionId;
    places.NamespaceReady = true;

    for (u32 i = 0; i < layout.IconCount; ++i) {
        PlaceKind kind = KindFromIcon(layout.Icons[i].Kind);
        ShellPlace* place = AddOrGetPlace(places, kind, layout.Icons[i].Label);
        if (place) place->OnDesktop = true;
    }

    for (u32 i = 0; i < menu.ItemCount; ++i) {
        const SHELLSTART::StartItem& item = menu.Items[i];
        if (!item.Enabled ||
            (item.Kind != SHELLSTART::StartItemKind::Place &&
             item.Kind != SHELLSTART::StartItemKind::System)) {
            continue;
        }

        PlaceKind kind{};
        if (!TryKindFromTarget(item.Target, kind)) continue;

        ShellPlace* place = AddOrGetPlace(places, kind, item.Label);
        if (place) place->InStartMenu = true;
    }

    for (u32 i = 0; i < places.PlaceCount; ++i) {
        if (places.Places[i].Kind == PlaceKind::Computer) {
            places.DefaultPlaceIndex = i;
            break;
        }
    }

    Debug::Printf("[SHELLPLACES] Session %u namespace built (%u places)\r\n",
                  places.SessionId, places.PlaceCount);
    return places.PlaceCount > 0;
}

bool PublishPlaceState(PlaceState& places) {
    if (!places.NamespaceReady || places.PlaceCount == 0 ||
        places.DefaultPlaceIndex >= places.PlaceCount) {
        return false;
    }

    places.Published = true;
    Debug::Printf("[SHELLPLACES] Session %u default place '%s'\r\n",
                  places.SessionId,
                  places.Places[places.DefaultPlaceIndex].Label);
    return true;
}

} // namespace SHELLPLACES
