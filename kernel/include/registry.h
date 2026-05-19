#pragma once
// registry.h -- MicroNT in-memory system registry bootstrap.

namespace REGISTRY {

void Init();
bool LoadSystemHive();
const char* QueryString(const char* key_path, const char* value_name);

} // namespace REGISTRY
