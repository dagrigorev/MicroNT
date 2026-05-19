// registry.cpp -- MicroNT in-memory system registry bootstrap.

#include "../include/registry.h"
#include "../include/debug.h"

namespace REGISTRY {

struct StringValue {
    const char* KeyPath;
    const char* ValueName;
    const char* Data;
};

static bool s_system_hive_loaded = false;

static constexpr const char* WINLOGON_KEY =
    "\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";

static constexpr StringValue SYSTEM_VALUES[] = {
    { WINLOGON_KEY, "Shell", "explorer.exe" },
    { WINLOGON_KEY, "Userinit", "userinit.exe" },
};

static bool StrEq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

void Init() {
    s_system_hive_loaded = false;
    Debug::Print("[REGISTRY] Registry subsystem initialized\r\n");
}

bool LoadSystemHive() {
    s_system_hive_loaded = true;
    Debug::Print("[REGISTRY] System hive loaded\r\n");
    Debug::Print("[REGISTRY] HKLM Winlogon values ready\r\n");
    return true;
}

const char* QueryString(const char* key_path, const char* value_name) {
    if (!s_system_hive_loaded) return nullptr;

    for (const auto& value : SYSTEM_VALUES) {
        if (StrEq(value.KeyPath, key_path) &&
            StrEq(value.ValueName, value_name)) {
            return value.Data;
        }
    }
    return nullptr;
}

} // namespace REGISTRY
