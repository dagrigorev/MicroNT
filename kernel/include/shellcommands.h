#pragma once
// shellcommands.h -- MicroNT shell command entrypoint model.

#include "shellstart.h"
#include "ntdef.h"

namespace SHELLCOMMANDS {

enum class CommandKind : u32 {
    Search,
    Run,
};

struct ShellCommand {
    CommandKind Kind;
    const char* Label;
    const char* Target;
    bool Enabled;
    bool DialogReady;
};

struct CommandState {
    u32 SessionId;
    ShellCommand Commands[8];
    u32 CommandCount;
    u32 DefaultCommandIndex;
    bool Published;
};

void Init();
bool BuildCommandState(CommandState& commands,
                       const SHELLSTART::StartMenuState& menu);
bool PublishCommandState(CommandState& commands);

} // namespace SHELLCOMMANDS
