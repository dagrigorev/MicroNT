#pragma once
// userinit.h -- MicroNT user profile and shell launcher boundary.

#include "appmodel.h"
#include "ntdef.h"
#include "profile.h"
#include "process.h"
#include "session.h"

namespace USERINIT {

struct ShellLaunchResult {
    KProcess* Process;
    KThread*  Thread;
};

void Init();
bool PrepareInteractiveUser(u32 session_id, PROFILE::UserProfile& profile);
ShellLaunchResult LaunchShell(u32 session_id, const char* shell_name,
                              const APPMODEL::AppIdentity& identity,
                              const SM::ShellImageConfig& cfg);

} // namespace USERINIT
