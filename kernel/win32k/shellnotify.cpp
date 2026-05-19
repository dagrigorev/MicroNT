// shellnotify.cpp -- MicroNT shell notification queue model.

#include "../include/shellnotify.h"
#include "../include/debug.h"

namespace SHELLNOTIFY {

void Init() {
    Debug::Print("[SHELLNOTIFY] Shell notification service initialized\r\n");
}

bool CreateNotificationQueue(NotificationQueue& queue,
                             const SHELLTRAY::TrayState& tray) {
    if (!tray.Published) return false;

    queue = {};
    queue.SessionId = tray.SessionId;
    queue.CenterReady = true;

    Debug::Printf("[SHELLNOTIFY] Session %u notification center ready\r\n",
                  queue.SessionId);
    return true;
}

bool PublishWelcomeNotification(NotificationQueue& queue) {
    if (!queue.CenterReady || queue.Count >= 4) return false;

    Notification& item = queue.Items[queue.Count++];
    item.AppName = "MicroNT Shell";
    item.Title = "Desktop ready";
    item.Body = "Interactive shell environment initialized";
    item.Visible = true;

    Debug::Printf("[SHELLNOTIFY] Session %u notification '%s' from %s\r\n",
                  queue.SessionId, item.Title, item.AppName);
    return true;
}

} // namespace SHELLNOTIFY
