// kernel32.cpp -- MicroNT kernel32.dll: a real Win32 surface over NT syscalls.
// Built freestanding by scripts/build-user.ps1 (clang + lld-link /dll) and
// loaded by the kernel; EXEs import these by name.
#include <stdint.h>

using i64 = int64_t;
using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;

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

enum { NT_TERMINATE_THREAD = 1, NT_WRITE_FILE = 4, NT_ALLOC_VM = 10 };

// PEB pointer from the TEB (GS:[0x60]).
static inline u64 peb() {
    u64 p;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(p));
    return p;
}

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

// Heap: PEB.ProcessHeap (PEB+0x30) is the default heap handle. HeapAlloc backs
// each allocation with NtAllocateVirtualMemory (zeroed). HeapFree is a no-op
// (the kernel allocator is bump-only for now).
__declspec(dllexport) void* GetProcessHeap() {
    return reinterpret_cast<void*>(*reinterpret_cast<volatile u64*>(peb() + 0x30));
}
__declspec(dllexport) void* HeapAlloc(void* /*hHeap*/, u32 /*flags*/, u64 bytes) {
    if (!bytes) bytes = 1;
    i64 va = sc(NT_ALLOC_VM, (i64)bytes, 0, 0);
    return reinterpret_cast<void*>((u64)va);
}
__declspec(dllexport) int HeapFree(void* /*hHeap*/, u32 /*flags*/, void* /*mem*/) {
    return 1;
}

// GetModuleHandle(NULL) -> the EXE's own base (PEB.ImageBaseAddress @0x10).
__declspec(dllexport) void* GetModuleHandleW(const void* name) {
    if (name) return nullptr;
    return reinterpret_cast<void*>(*reinterpret_cast<volatile u64*>(peb() + 0x10));
}

// GetCommandLineW -> PEB.ProcessParameters(0x20).CommandLine.Buffer(0x78).
__declspec(dllexport) const u16* GetCommandLineW() {
    u64 params = *reinterpret_cast<volatile u64*>(peb() + 0x20);
    if (!params) return nullptr;
    return reinterpret_cast<const u16*>(*reinterpret_cast<volatile u64*>(params + 0x78));
}

} // extern "C"
