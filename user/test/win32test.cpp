// win32test.cpp -- a Win32 EXE that imports kernel32.dll (built by the user
// pipeline). Proves PE import resolution against our own compiled DLL whose
// functions wrap NT syscalls.
#include <stdint.h>

extern "C" {
__declspec(dllimport) void* GetStdHandle(uint32_t);
__declspec(dllimport) int   WriteFile(void*, const void*, uint32_t, uint32_t*, void*);
__declspec(dllimport) void  ExitProcess(uint32_t);
__declspec(dllimport) uint32_t GetCurrentProcessId();
__declspec(dllimport) void* GetProcessHeap();
__declspec(dllimport) void* HeapAlloc(void*, uint32_t, uint64_t);
__declspec(dllimport) void* GetModuleHandleW(const void*);
}

enum { STD_OUTPUT_HANDLE = (uint32_t)-11 };

static uint32_t slen(const char* s) { uint32_t n = 0; while (s[n]) ++n; return n; }

extern "C" void Entry() {
    void* out = GetStdHandle(STD_OUTPUT_HANDLE);
    uint32_t written = 0;

    static const char msg[] = "WIN32 OK via kernel32 (WriteFile/ExitProcess)\n";
    WriteFile(out, msg, sizeof(msg) - 1, &written, nullptr);

    // Exercise the heap: GetProcessHeap + HeapAlloc -> writable memory.
    void* heap = GetProcessHeap();
    char* buf = (char*)HeapAlloc(heap, 0, 64);
    if (buf && heap) {
        const char* m = "HEAP OK: kernel32 GetProcessHeap+HeapAlloc work\n";
        uint32_t i = 0;
        for (; m[i]; ++i) buf[i] = m[i];
        WriteFile(out, buf, i, &written, nullptr);
    }

    // Touch more imports so the IAT carries several entries.
    (void)GetCurrentProcessId();
    (void)GetModuleHandleW(nullptr);
    ExitProcess(0);
}
