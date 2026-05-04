#pragma once
// spinlock.h - MicroNT simple test-and-set spinlock

#include "ntdef.h"

// ============================================================
// Spinlock
// Uses GCC/Clang built-in atomics so no <atomic> header needed.
// ============================================================
struct Spinlock {
    volatile u32 _lock = 0;

    void Acquire() noexcept {
        while (__sync_lock_test_and_set(&_lock, 1u)) {
            // PAUSE: reduces power consumption and improves
            // performance on hyperthreaded CPUs while spinning.
            __asm__ volatile("pause");
        }
    }

    void Release() noexcept {
        __sync_lock_release(&_lock);
    }

    bool TryAcquire() noexcept {
        return __sync_lock_test_and_set(&_lock, 1u) == 0;
    }

    bool IsLocked() const noexcept {
        return _lock != 0;
    }
};

// RAII guard
struct SpinlockGuard {
    explicit SpinlockGuard(Spinlock& sl) noexcept : _sl(sl) { _sl.Acquire(); }
    ~SpinlockGuard() noexcept { _sl.Release(); }

    SpinlockGuard(const SpinlockGuard&)            = delete;
    SpinlockGuard& operator=(const SpinlockGuard&) = delete;

private:
    Spinlock& _sl;
};
