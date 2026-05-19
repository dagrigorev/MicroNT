#pragma once
// services.h -- MicroNT Session 0 service control plane.

#include "ntdef.h"

namespace SERVICES {

struct ServiceControlPlane {
    u32  SessionId;
    bool ControlPlaneReady;
    bool CoreServicesStarted;
};

void Init();
bool StartControlPlane(ServiceControlPlane& services, u32 session_id);
bool StartCoreServices(ServiceControlPlane& services);

} // namespace SERVICES
