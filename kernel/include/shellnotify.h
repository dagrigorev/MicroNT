#pragma once
// shellnotify.h -- MicroNT shell notification queue model.

#include "shelltray.h"
#include "ntdef.h"

namespace SHELLNOTIFY {

struct Notification {
    const char* AppName;
    const char* Title;
    const char* Body;
    bool Visible;
};

struct NotificationQueue {
    u32 SessionId;
    Notification Items[4];
    u32 Count;
    bool CenterReady;
};

void Init();
bool CreateNotificationQueue(NotificationQueue& queue,
                             const SHELLTRAY::TrayState& tray);
bool PublishWelcomeNotification(NotificationQueue& queue);

} // namespace SHELLNOTIFY
