#pragma once
// profile.h -- MicroNT user profile bootstrap boundary.

#include "ntdef.h"

namespace PROFILE {

struct UserProfile {
    u32 SessionId;
    const char* UserName;
    const char* HomeDirectory;
    bool HiveLoaded;
    bool EnvironmentReady;
};

void Init();
bool LoadUserProfile(UserProfile& profile, u32 session_id, const char* user_name);
bool ApplyEnvironment(UserProfile& profile);

} // namespace PROFILE
