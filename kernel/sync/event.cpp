// event.cpp - MicroNT M12 KEvent implementation

#include "../include/sync.h"
#include "../include/process.h"
#include "../include/memory.h"
#include "../include/debug.h"
#include "../include/hal.h"

namespace SYNC {

void Init() {
    KDBG_INFO("SYNC: synchronization layer initialized");
}

void EventInit(KEvent* ev, bool auto_reset, bool initially_signaled) {
    ev->signaled    = initially_signaled;
    ev->auto_reset  = auto_reset;
    ev->waiters     = nullptr;
}

// ============================================================
// EventSet - signal the event
//
// Protocol (single-CPU, interrupt-based locking):
//   Disable interrupts around the signaled flag and waiter list
//   to prevent the timer ISR from context-switching mid-check.
// ============================================================
void EventSet(KEvent* ev) {
    HAL::DisableInterrupts();

    if (ev->waiters) {
        // Wake the first waiter
        KThread* t = ev->waiters;
        ev->waiters = t->WaitNext;
        t->WaitNext = nullptr;

        // For auto-reset: event stays unsignaled (consumed by the woken thread)
        // For manual-reset: event stays signaled
        if (!ev->auto_reset) {
            ev->signaled = true;
        }

        // Unblock the thread (re-enables interrupts internally via QueuePushBack,
        // but we'll re-enable here too for safety)
        t->State = ThreadState::READY;
        // Add directly to queue (interrupts already disabled)
        // We call Sched::UnblockThread which will disable/enable around the push
        HAL::EnableInterrupts();
        Sched::UnblockThread(t);
        KDBG_TRACE("SYNC: EventSet: unblocked thread '%s' TID=%u",
                   t->Name, t->Tid);
    } else {
        ev->signaled = true;
        HAL::EnableInterrupts();
        KDBG_TRACE("SYNC: EventSet: signaled (no waiters)");
    }
}

// ============================================================
// EventReset - clear the event
// ============================================================
void EventReset(KEvent* ev) {
    HAL::DisableInterrupts();
    ev->signaled = false;
    HAL::EnableInterrupts();
}

// ============================================================
// EventWait - wait for the event to be signaled
// ============================================================
NTSTATUS EventWait(KEvent* ev, u32 timeout_ms) {
    // Fast path: already signaled
    HAL::DisableInterrupts();
    if (ev->signaled) {
        if (ev->auto_reset) ev->signaled = false;
        HAL::EnableInterrupts();
        return STATUS_SUCCESS;
    }

    // Timeout = 0: non-blocking check
    if (timeout_ms == 0) {
        HAL::EnableInterrupts();
        return STATUS_TIMEOUT;
    }

    // Blocking path: add to waiter list, set BLOCKED, then yield.
    // Interrupts are disabled here so the scheduler won't preempt us
    // between adding to waiters and calling Schedule().
    KThread* t = Sched::CurrentThread();
    if (!t) {
        HAL::EnableInterrupts();
        return STATUS_UNSUCCESSFUL;
    }

    t->WaitNext  = ev->waiters;
    ev->waiters  = t;
    // Set BLOCKED before re-enabling interrupts so ScheduleInternal
    // sees the correct state and does NOT re-queue this thread.
    t->State = ThreadState::BLOCKED;

    HAL::EnableInterrupts();
    Sched::Schedule();   // switches away; returns when EventSet unblocks us

    // Re-check in case of future spurious wakeups (defensive)
    return STATUS_SUCCESS;
}

// ============================================================
// EventAlloc - heap-allocate a KEvent (for handle table)
// ============================================================
KEvent* EventAlloc(bool auto_reset, bool initially_signaled) {
    auto* ev = static_cast<KEvent*>(
        KernelHeap::AllocZeroed(sizeof(KEvent), alignof(KEvent)));
    if (!ev) return nullptr;
    EventInit(ev, auto_reset, initially_signaled);
    return ev;
}

} // namespace SYNC
