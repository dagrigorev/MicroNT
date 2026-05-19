// shellcommands.cpp -- MicroNT shell command entrypoint model.

#include "../include/shellcommands.h"
#include "../include/debug.h"

namespace SHELLCOMMANDS {

static bool StringEquals(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

static bool TryAddCommand(CommandState& commands,
                          const SHELLSTART::StartItem& item) {
    if (item.Kind != SHELLSTART::StartItemKind::Command ||
        !item.Enabled || commands.CommandCount >= 8) {
        return false;
    }

    CommandKind kind{};
    if (StringEquals(item.Target, "shell:search")) {
        kind = CommandKind::Search;
    } else if (StringEquals(item.Target, "shell:run")) {
        kind = CommandKind::Run;
    } else {
        return false;
    }

    ShellCommand& command = commands.Commands[commands.CommandCount++];
    command.Kind = kind;
    command.Label = item.Label;
    command.Target = item.Target;
    command.Enabled = true;
    command.DialogReady = true;
    return true;
}

void Init() {
    Debug::Print("[SHELLCOMMANDS] Shell command broker initialized\r\n");
}

bool BuildCommandState(CommandState& commands,
                       const SHELLSTART::StartMenuState& menu) {
    if (!menu.Published || menu.SessionId == 0) return false;

    commands = {};
    commands.SessionId = menu.SessionId;

    for (u32 i = 0; i < menu.ItemCount; ++i) {
        TryAddCommand(commands, menu.Items[i]);
    }

    if (commands.CommandCount == 0) return false;
    commands.DefaultCommandIndex = 0;

    Debug::Printf("[SHELLCOMMANDS] Session %u command state built (%u commands)\r\n",
                  commands.SessionId, commands.CommandCount);
    return true;
}

bool PublishCommandState(CommandState& commands) {
    if (commands.CommandCount == 0 ||
        commands.DefaultCommandIndex >= commands.CommandCount) {
        return false;
    }

    commands.Published = true;
    Debug::Printf("[SHELLCOMMANDS] Session %u default command '%s'\r\n",
                  commands.SessionId,
                  commands.Commands[commands.DefaultCommandIndex].Label);
    return true;
}

} // namespace SHELLCOMMANDS
