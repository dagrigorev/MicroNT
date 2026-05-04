// kernel32.cpp - MicroNT kernel32.dll stub
// TODO(M8): Forward to ntdll syscall stubs and implement Win32 wrappers.

#include <stdint.h>

using BOOL   = int;
using DWORD  = uint32_t;
using HANDLE = void*;
using PVOID  = void*;
using LPVOID = void*;

constexpr HANDLE INVALID_HANDLE_VALUE = reinterpret_cast<HANDLE>(-1LL);
constexpr DWORD  STD_INPUT_HANDLE  = (DWORD)-10;
constexpr DWORD  STD_OUTPUT_HANDLE = (DWORD)-11;
constexpr DWORD  STD_ERROR_HANDLE  = (DWORD)-12;

extern "C" {

// GetStdHandle — TODO(M8): return actual console object handles
__declspec(dllexport)
HANDLE GetStdHandle(DWORD nStdHandle) {
    // Encode the handle number directly for now
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(nStdHandle));
}

// WriteFile — TODO(M8): issue NtWriteFile syscall
__declspec(dllexport)
BOOL WriteFile(HANDLE hFile, PVOID lpBuffer, DWORD nNumberOfBytesToWrite,
               DWORD* lpNumberOfBytesWritten, PVOID lpOverlapped) {
    // Stub: claim success, write 0 bytes
    if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = 0;
    return 1;
}

// ExitProcess — TODO(M8): NtTerminateProcess
__declspec(dllexport)
[[noreturn]] void ExitProcess(DWORD uExitCode) {
    for (;;) __asm__("hlt");
}

// GetLastError — TODO(M8): TEB-based
__declspec(dllexport)
DWORD GetLastError() { return 0; }

__declspec(dllexport)
void SetLastError(DWORD dwErrCode) { (void)dwErrCode; }

} // extern "C"
