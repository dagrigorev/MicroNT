// kernel32.cpp -- MicroNT kernel32.dll: a real Win32 surface over NT syscalls.
// Built freestanding by scripts/build-user.ps1 (clang + lld-link /dll) and
// loaded by the kernel; EXEs import these by name.
#include <stdint.h>

using i64 = int64_t;
using u64 = uint64_t;
using u32 = uint32_t;

// MicroNT syscall ABI: number in RAX, args RDI/RSI/RDX, result RAX.
static inline i64 sc(i64 n, i64 a1, i64 a2, i64 a3) {
    i64 r;
    // The kernel's syscall entry rearranges args through r8/r9/r10 and SYSRET
    // clobbers rcx/r11. All must be in the clobber list, or the compiler will
    // wrongly assume callee args kept in r8/r9 survive the call.
    __asm__ volatile("syscall" : "=a"(r)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "r8", "r9", "r10", "memory");
    return r;
}

enum { NT_TERMINATE_THREAD = 1, NT_WRITE_FILE = 4 };

extern "C" {

// Std handles encode directly to the kernel's console handle (stdout = 1).
__declspec(dllexport) void* GetStdHandle(u32 /*nStdHandle*/) {
    return reinterpret_cast<void*>((u64)1);
}

__declspec(dllexport) int WriteFile(void* hFile, const void* buf, u32 len,
                                    u32* written, void* /*overlapped*/) {
    i64 n = sc(NT_WRITE_FILE, (i64)(u64)hFile, (i64)(u64)buf, (i64)len);
    if (written) *written = (u32)n;
    return 1;
}

__declspec(dllexport) void ExitProcess(u32 code) {
    sc(NT_TERMINATE_THREAD, (i64)(u64)code, 0, 0);
    for (;;) {}
}

// TEB-backed (GS): LastErrorValue @ TEB+0x68.
__declspec(dllexport) u32 GetLastError() {
    u32 e;
    __asm__ volatile("movl %%gs:0x68, %0" : "=r"(e));
    return e;
}
__declspec(dllexport) void SetLastError(u32 code) {
    __asm__ volatile("movl %0, %%gs:0x68" :: "r"(code) : "memory");
}

// TEB.ClientId.UniqueProcess/Thread @ TEB+0x40 / +0x48.
__declspec(dllexport) u32 GetCurrentProcessId() {
    u64 pid;
    __asm__ volatile("movq %%gs:0x40, %0" : "=r"(pid));
    return (u32)pid;
}
__declspec(dllexport) u32 GetCurrentThreadId() {
    u64 tid;
    __asm__ volatile("movq %%gs:0x48, %0" : "=r"(tid));
    return (u32)tid;
}

// KUSER_SHARED_DATA: GetTickCount = TickCountLow(0x320) * Multiplier(0x004) >> 24.
__declspec(dllexport) u32 GetTickCount() {
    volatile u32* k = reinterpret_cast<volatile u32*>(0x7FFE0000ULL);
    u64 lo  = k[0x320 / 4];
    u64 mul = k[0x004 / 4];
    return (u32)((lo * mul) >> 24);
}

} // extern "C"
