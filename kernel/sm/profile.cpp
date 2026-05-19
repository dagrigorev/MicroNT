// profile.cpp -- MicroNT user profile loader.

#include "../include/profile.h"
#include "../include/debug.h"

namespace PROFILE {

void Init() {
    Debug::Print("[PROFILE] Profile subsystem initialized\r\n");
}

bool LoadUserProfile(UserProfile& profile, u32 session_id, const char* user_name) {
    profile.SessionId = session_id;
    profile.UserName = user_name ? user_name : "DefaultUser";
    profile.HomeDirectory = "\\Users\\DefaultUser";
    profile.HiveLoaded = true;
    profile.EnvironmentReady = false;

    Debug::Printf("[PROFILE] Session %u profile '%s' loaded\r\n",
                  profile.SessionId, profile.UserName);
    return true;
}

bool ApplyEnvironment(UserProfile& profile) {
    if (!profile.HiveLoaded) return false;

    profile.EnvironmentReady = true;
    Debug::Printf("[PROFILE] Session %u environment ready (%s)\r\n",
                  profile.SessionId, profile.HomeDirectory);
    return true;
}

} // namespace PROFILE
