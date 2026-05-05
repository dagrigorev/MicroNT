#pragma once
// io.h - MicroNT I/O Manager interface

#include "ntdef.h"
#include "ntstatus.h"

// ============================================================
// I/O Manager (stub for M1)
// ============================================================
namespace IO {

void Init();

// Console subsystem
namespace Console {
    void Init();
    void Write(const char* buf, usize len);
    void WriteChar(char c);
    usize Read(char* buf, usize max_len);
}

} // namespace IO

// ============================================================
// Syscall layer (stub for M1)
// ============================================================
namespace SYSCALL {
void Init();
void SetCommands(const char** cmds, u32 count);
} // namespace SYSCALL
