#pragma once
// sync.h - MicroNT M12 synchronization primitives

#include "ntdef.h"
#include "ntstatus.h"

struct KThread;

// ============================================================
// KEvent - manual-reset or auto-reset event object
// ============================================================
struct KEvent {
    volatile bool  signaled;
    bool           auto_reset;   // true: reset after first successful wait
    u32            _pad;
    KThread*       waiters;      // head of singly-linked waiter list (via WaitNext)
};

namespace SYNC {

void Init();

// ---- Event API (used by kernel and syscall layer) ----------

// Initialize an event in-place (no heap alloc needed for kernel events).
void EventInit(KEvent* ev, bool auto_reset = false, bool initially_signaled = false);

// Signal the event.  If any thread is waiting, the first one is unblocked.
void EventSet(KEvent* ev);

// Clear the signal.
void EventReset(KEvent* ev);

// Wait for the event to become signaled.
//   timeout_ms = 0          -> return immediately (STATUS_TIMEOUT if not signaled)
//   timeout_ms = 0xFFFFFFFF -> wait indefinitely
// Returns STATUS_SUCCESS on signal, STATUS_TIMEOUT on timeout.
NTSTATUS EventWait(KEvent* ev, u32 timeout_ms = 0xFFFFFFFF);

// ---- Object-based variants (used by syscall layer) ---------
// Allocate a KEvent via the kernel heap (for handles).
KEvent* EventAlloc(bool auto_reset, bool initially_signaled);

} // namespace SYNC
