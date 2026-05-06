// scheduler.cpp - MicroNT M5 Round-Robin Scheduler

#include "../include/process.h"
#include "../include/debug.h"
#include "../include/hal.h"

namespace Sched {

// ============================================================
// State
// ============================================================
// M15: Three priority-level circular ready queues (HIGH=2, NORMAL=1, LOW=0).
// Each queue has its own sentinel node.
static KThread  s_sentinel[THREAD_PRIORITY_COUNT];
static KThread* s_current  = nullptr;
static bool     s_active   = false;
static u32      s_quantum  = QUANTUM_TICKS;

// ============================================================
// Internal helpers (interrupts must be disabled by caller)
// ============================================================
static inline u32 ClampPri(u32 p) {
    return p < THREAD_PRIORITY_COUNT ? p : THREAD_PRIORITY_NORMAL;
}

static void QueuePushBack(KThread* t) {
    u32 pri = ClampPri(t->Priority);
    KThread* sen = &s_sentinel[pri];
    t->Next       = sen;
    t->Prev       = sen->Prev;
    sen->Prev->Next = t;
    sen->Prev       = t;
}

static void QueueRemove(KThread* t) {
    if (!t->Next || !t->Prev) return;  // not in queue
    t->Prev->Next = t->Next;
    t->Next->Prev = t->Prev;
    t->Next = nullptr;
    t->Prev = nullptr;
}

static KThread* QueuePop() {
    // Scan from highest priority to lowest; return first available thread.
    for (i32 pri = THREAD_PRIORITY_COUNT - 1; pri >= 0; --pri) {
        KThread* sen = &s_sentinel[pri];
        if (sen->Next != sen) {         // non-empty
            KThread* t = sen->Next;
            QueueRemove(t);
            return t;
        }
    }
    return nullptr;
}

// ============================================================
// Init / Start
// ============================================================
void Init() {
    // Set up three empty circular sentinel lists (one per priority)
    for (u32 i = 0; i < THREAD_PRIORITY_COUNT; ++i) {
        s_sentinel[i].Next = &s_sentinel[i];
        s_sentinel[i].Prev = &s_sentinel[i];
    }

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

    // Re-add prev to ready queue ONLY if it's still runnable.
    // TERMINATED: don't re-queue (process clean-up path).
    // BLOCKED: don't re-queue either (blocked on event/mutex/sleep);
    //          the unblock path (EventSet, timer, etc.) will re-add it.
    // Only re-queue if the thread was RUNNING (normal preemption).
    if (prev->State == ThreadState::RUNNING) {
        prev->State = ThreadState::READY;
        QueuePushBack(prev);
    }

    // Activate next
    s_current = next;
    next->State = ThreadState::RUNNING;
    s_quantum   = QUANTUM_TICKS;

    // Switch address space if the new thread belongs to a different process
    if (next->Process) {
        u64 cur_cr3 = HAL::ReadCr3() & 0x000FFFFFFFFFF000ULL;
        if (next->Process->Cr3 != cur_cr3) {
            __asm__ volatile("mov %0, %%cr3" :: "r"(next->Process->Cr3) : "memory");
        }
    }

    // Update TSS.RSP0 so hardware interrupts from ring-3 land on the right stack
    HAL::SetTSSRsp0(next->KernelStackBase + next->KernelStackSize);

    // Actual register/stack swap
    switch_context(prev, next);
    // Returns here when 'prev' is scheduled back.
    // Interrupts are still disabled (caller's responsibility to re-enable).
}

// ============================================================
// Tick - called from IRQ0 handler (interrupts already disabled)
// ============================================================
static void WakeSleepers();  // forward declaration

void Tick() {
    if (!s_active || !s_current) return;
    WakeSleepers();   // wake any threads whose sleep deadline has passed
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

// ============================================================
// Block / Unblock
// ============================================================
namespace Sched {

void BlockCurrentThread() {
    // Must be called with interrupts disabled.
    // Removes current thread from ready consideration;
    // caller must call Schedule() to switch away.
    if (!s_current) return;
    s_current->State = ThreadState::BLOCKED;
    // The thread is not in the ready queue (it's s_current), so nothing to remove.
}

void UnblockThread(KThread* t) {
    if (!t || t->State == ThreadState::TERMINATED) return;
    HAL::DisableInterrupts();
    t->State = ThreadState::READY;
    QueuePushBack(t);
    HAL::EnableInterrupts();
}

} // namespace Sched

// ============================================================
// Sleep list
// ============================================================
namespace Sched {

static KThread* s_sleep_head = nullptr;

void Sleep(u32 ms) {
    if (ms == 0) return;
    // Convert ms to PIT ticks (100 Hz: 1 tick = 10 ms)
    u64 ticks_needed = ((u64)ms * 100 + 999) / 1000;  // round up
    if (ticks_needed == 0) ticks_needed = 1;

    HAL::DisableInterrupts();
    KThread* t = s_current;
    if (!t) { HAL::EnableInterrupts(); return; }

    t->SleepUntil  = HAL::PitTicks() + ticks_needed;
    t->SleepNext   = s_sleep_head;
    s_sleep_head   = t;
    t->State       = ThreadState::BLOCKED;

    HAL::EnableInterrupts();
    Sched::Schedule();  // switch away; returns when timer wakes us
}

} // namespace Sched (sleep additions)

// ============================================================
// Wake sleeping threads on each tick (called from Tick())
// ============================================================
namespace Sched {

static void WakeSleepers() {
    // Must be called with interrupts disabled (from Tick())
    u64 now = HAL::PitTicks();
    KThread** pp = &s_sleep_head;
    while (*pp) {
        KThread* t = *pp;
        if (t->SleepUntil <= now) {
            // Remove from sleep list
            *pp = t->SleepNext;
            t->SleepNext  = nullptr;
            t->SleepUntil = 0;
            // Add to ready queue (interrupts already disabled)
            t->State = ThreadState::READY;
            QueuePushBack(t);
        } else {
            pp = &t->SleepNext;
        }
    }
}

} // namespace Sched (waker)
