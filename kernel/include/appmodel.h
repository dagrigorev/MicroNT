#pragma once
// appmodel.h -- MicroNT application identity/bootstrap boundary.

#include "ntdef.h"

namespace APPMODEL {

struct AppIdentity {
    u32 SessionId;
    const char* AppId;
    const char* ImageName;
    bool Registered;
};

void Init();
bool RegisterShellApp(AppIdentity& identity, u32 session_id,
                      const char* image_name);
bool ActivateShellApp(AppIdentity& identity);

} // namespace APPMODEL
