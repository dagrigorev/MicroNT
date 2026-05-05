// scheduler.cpp - MicroNT M5 Round-Robin Scheduler

#include "../include/process.h"
#include "../include/debug.h"
#include "../include/hal.h"

namespace Sched {

// ============================================================
// State
// ============================================================
// Sentinel node for the circular doubly-linked ready queue.
// sentinel.Next = head (first ready thread)
// sentinel.Prev = tail (last ready thread)
// Empty queue: sentinel.Next == sentinel.Prev == &s_sentinel
static KThread  s_sentinel;
static KThread* s_current  = nullptr;
static bool     s_active   = false;
static u32      s_quantum  = QUANTUM_TICKS;

// ============================================================
// Internal helpers (interrupts must be disabled by caller)
// ============================================================
static void QueuePushBack(KThread* t) {
    t->Next              = &s_sentinel;
    t->Prev              = s_sentinel.Prev;
    s_sentinel.Prev->Next = t;
    s_sentinel.Prev       = t;
}

static void QueueRemove(KThread* t) {
    if (!t->Next || !t->Prev) return;  // not in queue
    t->Prev->Next = t->Next;
    t->Next->Prev = t->Prev;
    t->Next = nullptr;
    t->Prev = nullptr;
}

static KThread* QueuePop() {
    if (s_sentinel.Next == &s_sentinel) return nullptr;  // empty
    KThread* t = s_sentinel.Next;
    QueueRemove(t);
    return t;
}

// ============================================================
// Init / Start
// ============================================================
void Init() {
    // Set up empty circular sentinel list
    s_sentinel.Next = &s_sentinel;
    s_sentinel.Prev = &s_sentinel;

    // s_current = the main thread (set by PS::Init which calls us)
    // We need PS::MainThread() here, but process.h declares both.
    // PS::Init() will call us and then set s_current via Start().
}

void Start() {
    // Main thread (kernel_main) becomes s_current
    s_current = PS::MainThread();
    s_current->State = ThreadState::RUNNING;
    s_quantum = QUANTUM_TICKS;
    s_active  = true;
    KDBG_INFO("Sched: started (current='%s')", s_current->Name);
}

// ============================================================
// Public API
// ============================================================
void AddThread(KThread* t) {
    HAL::DisableInterrupts();
    t->State = ThreadState::READY;
    QueuePushBack(t);
    HAL::EnableInterrupts();
    KDBG_TRACE("Sched: AddThread '%s' TID=%u", t->Name, t->Tid);
}

void RemoveThread(KThread* t) {
    HAL::DisableInterrupts();
    QueueRemove(t);
    HAL::EnableInterrupts();
}

KThread* CurrentThread() { return s_current; }
bool     IsActive()      { return s_active; }

// ============================================================
// Schedule - core context switch logic
// Must be called with interrupts DISABLED.
// ============================================================
static void ScheduleInternal() {
    if (!s_active || !s_current) return;

    KThread* next = QueuePop();
    if (!next) return;                     // queue empty (only idle would be here)

    if (next == s_current) {
        // Only one runnable thread: put it back, reset quantum, continue
        QueuePushBack(next);
        s_quantum = QUANTUM_TICKS;
        return;
    }

    KThread* prev = s_current;

    // Re-add prev to ready queue (if not terminated)
    if (prev->State != ThreadState::TERMINATED) {
        prev->State = ThreadState::READY;
        QueuePushBack(prev);
    }

    // Activate next
    s_current = next;
    next->State = ThreadState::RUNNING;
    s_quantum   = QUANTUM_TICKS;

    // Update TSS.RSP0 to top of new thread's kernel stack
    HAL::SetTSSRsp0(next->KernelStackBase + next->KernelStackSize);

    // Actual register/stack swap
    switch_context(prev, next);
    // Returns here when 'prev' is scheduled back.
    // Interrupts are still disabled (caller's responsibility to re-enable).
}

// ============================================================
// Tick - called from IRQ0 handler (interrupts already disabled)
// ============================================================
void Tick() {
    if (!s_active || !s_current) return;
    if (s_quantum > 0) {
        --s_quantum;
        if (s_quantum == 0) {
            ScheduleInternal();   // preempt: interrupts disabled, ISR will IRETQ
            // After switch_context returns here, we're back in 'prev' thread.
            // ISR will continue: EOI, then IRETQ which restores original RFLAGS (IF=1).
        }
    }
}

// ============================================================
// Schedule - called from user code (Yield/sleep)
// Enables interrupts before returning.
// ============================================================
void Schedule() {
    HAL::DisableInterrupts();
    ScheduleInternal();
    HAL::EnableInterrupts();
}

} // namespace Sched
