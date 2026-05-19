#pragma once
// shellplaces.h -- MicroNT shell namespace/place model.

#include "desktopmodel.h"
#include "shellstart.h"
#include "ntdef.h"

namespace SHELLPLACES {

enum class PlaceKind : u32 {
    Documents,
    Computer,
    ControlPanel,
    Network,
    Media,
    RecycleBin,
};

struct ShellPlace {
    PlaceKind Kind;
    const char* Label;
    const char* Target;
    bool OnDesktop;
    bool InStartMenu;
    bool Enabled;
};

struct PlaceState {
    u32 SessionId;
    ShellPlace Places[12];
    u32 PlaceCount;
    u32 DefaultPlaceIndex;
    bool NamespaceReady;
    bool Published;
};

void Init();
bool BuildPlaceState(PlaceState& places,
                     const DESKTOPMODEL::DesktopLayout& layout,
                     const SHELLSTART::StartMenuState& menu);
bool PublishPlaceState(PlaceState& places);

} // namespace SHELLPLACES
