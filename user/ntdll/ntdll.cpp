// ntdll.cpp - MicroNT ntdll.dll stub (user-mode native API layer)
// TODO(M8): Implement real syscall stubs via SYSCALL/SYSRET.
// This file is compiled as a Windows DLL targeting the host (for development).

// ============================================================
// Minimal type definitions (no Windows headers)
// ============================================================
#include <stdint.h>

using NTSTATUS = int32_t;
using HANDLE   = void*;
using ULONG    = uint32_t;
using PVOID    = void*;
using BOOLEAN  = uint8_t;

constexpr NTSTATUS STATUS_SUCCESS          = 0x00000000;
constexpr NTSTATUS STATUS_NOT_IMPLEMENTED  = (NTSTATUS)0xC0000002;

// ============================================================
// Stub exports
// ============================================================
extern "C" {

// NtTerminateProcess — TODO(M6): issue syscall
__declspec(dllexport)
NTSTATUS NtTerminateProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus) {
    // TODO(M6): syscall stub
    return STATUS_NOT_IMPLEMENTED;
}

// NtWriteFile — TODO(M6): issue syscall
__declspec(dllexport)
NTSTATUS NtWriteFile(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine,
                     PVOID ApcContext, PVOID IoStatusBlock,
                     PVOID Buffer, ULONG Length,
                     PVOID ByteOffset, ULONG* Key) {
    // TODO(M6): syscall stub
    return STATUS_NOT_IMPLEMENTED;
}

} // extern "C"
