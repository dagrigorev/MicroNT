#pragma once
// debug.h - MicroNT kernel debug / logging interface

#include "ntdef.h"

namespace Debug {

void Init();
void PutChar(char c);
void Print(const char* str);
void Printf(const char* fmt, ...);

} // namespace Debug

// Logging macros with level prefix
#define KDBG_INFO(fmt, ...)  Debug::Printf("[INFO ] " fmt "\n", ##__VA_ARGS__)
#define KDBG_WARN(fmt, ...)  Debug::Printf("[WARN ] " fmt "\n", ##__VA_ARGS__)
#define KDBG_ERROR(fmt, ...) Debug::Printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define KDBG_TRACE(fmt, ...) Debug::Printf("[TRACE] " fmt "\n", ##__VA_ARGS__)

// Kernel panic
[[noreturn]] void KernelPanic(const char* msg);

#define KASSERT(cond) \
    do { if (!(cond)) { \
        Debug::Printf("[ASSERT] %s:%d: assertion failed: %s\n", \
            __FILE__, __LINE__, #cond); \
        KernelPanic("Assertion failure"); \
    } } while(0)
