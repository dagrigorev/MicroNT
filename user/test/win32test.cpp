// win32test.cpp -- a Win32 EXE that imports kernel32.dll (built by the user
// pipeline). Proves PE import resolution against our own compiled DLL whose
// functions wrap NT syscalls.
#include <stdint.h>

extern "C" {
__declspec(dllimport) void* GetStdHandle(uint32_t);
__declspec(dllimport) int   WriteFile(void*, const void*, uint32_t, uint32_t*, void*);
__declspec(dllimport) void  ExitProcess(uint32_t);
__declspec(dllimport) uint32_t GetCurrentProcessId();
}

enum { STD_OUTPUT_HANDLE = (uint32_t)-11 };

extern "C" void Entry() {
    static const char msg[] = "WIN32 OK via kernel32 (WriteFile/ExitProcess)\n";
    uint32_t written = 0;
    void* h = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(h, msg, sizeof(msg) - 1, &written, nullptr);
    // Touch another import so the IAT carries more than one entry.
    (void)GetCurrentProcessId();
    ExitProcess(0);
}
