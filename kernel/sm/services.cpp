// services.cpp -- MicroNT Session 0 service control plane.

#include "../include/services.h"
#include "../include/debug.h"

namespace SERVICES {

void Init() {
    Debug::Print("[SERVICES] Service control subsystem initialized\r\n");
}

bool StartControlPlane(ServiceControlPlane& services, u32 session_id) {
    services.SessionId = session_id;
    services.ControlPlaneReady = true;
    services.CoreServicesStarted = false;
    Debug::Printf("[SERVICES] Session %u service control plane ready\r\n",
                  session_id);
    return true;
}

bool StartCoreServices(ServiceControlPlane& services) {
    if (!services.ControlPlaneReady) return false;
    services.CoreServicesStarted = true;
    Debug::Printf("[SERVICES] Session %u core services started\r\n",
                  services.SessionId);
    return true;
}

} // namespace SERVICES
