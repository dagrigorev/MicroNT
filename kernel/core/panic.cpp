// panic.cpp - MicroNT kernel panic handler

#include "../include/debug.h"
#include "../include/hal.h"

[[noreturn]] void KernelPanic(const char* msg) {
    HAL::DisableInterrupts();
    Debug::Printf("\n");
    Debug::Printf("=======================================================\n");
    Debug::Printf("  MicroNT KERNEL PANIC\n");
    Debug::Printf("=======================================================\n");
    Debug::Printf("  %s\n", msg);
    Debug::Printf("=======================================================\n");
    Debug::Printf("System halted.\n");
    HAL::CpuHalt();
}

// C++ ABI stubs required by the linker in a freestanding environment
extern "C" {

void __cxa_pure_virtual() {
    KernelPanic("Pure virtual function called");
}

// Stack protector stub (disabled by default but provided for safety)
[[noreturn]] void __stack_chk_fail() {
    KernelPanic("Stack smashing detected");
}

// Provide a minimal __stack_chk_guard to satisfy the linker if -fstack-protector is used
__attribute__((weak)) uptr __stack_chk_guard = 0xDEADBEEFCAFEBABEULL;

} // extern "C"
