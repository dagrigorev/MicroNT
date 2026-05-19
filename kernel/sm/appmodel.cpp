// appmodel.cpp -- MicroNT application identity/bootstrap boundary.

#include "../include/appmodel.h"
#include "../include/debug.h"

namespace APPMODEL {

void Init() {
    Debug::Print("[APPMODEL] Application model initialized\r\n");
}

bool RegisterShellApp(AppIdentity& identity, u32 session_id,
                      const char* image_name) {
    if (session_id == 0 || !image_name) return false;

    identity.SessionId = session_id;
    identity.AppId = "MicroNT.Desktop.Explorer";
    identity.ImageName = image_name;
    identity.Registered = true;

    Debug::Printf("[APPMODEL] Session %u shell app '%s' registered (%s)\r\n",
                  identity.SessionId, identity.AppId, identity.ImageName);
    return true;
}

bool ActivateShellApp(AppIdentity& identity) {
    if (!identity.Registered) return false;

    Debug::Printf("[APPMODEL] Session %u shell app '%s' activated\r\n",
                  identity.SessionId, identity.AppId);
    return true;
}

} // namespace APPMODEL
