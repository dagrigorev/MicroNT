// winapp.cpp -- a stock-style console EXE that links kernel32 and is loaded
// from the VHD (not embedded in the kernel). Proves running a real on-disk
// Windows binary: imports resolve against kernel32, which wraps NT syscalls.
#include <stdint.h>

extern "C" {
__declspec(dllimport) void* GetStdHandle(uint32_t);
__declspec(dllimport) int   WriteFile(void*, const void*, uint32_t, uint32_t*, void*);
__declspec(dllimport) void  ExitProcess(uint32_t);
__declspec(dllimport) void* GetProcessHeap();
__declspec(dllimport) void* HeapAlloc(void*, uint32_t, uint64_t);
}

static uint32_t slen(const char* s) { uint32_t n = 0; while (s[n]) ++n; return n; }

extern "C" void Entry() {
    void* out = GetStdHandle((uint32_t)-11);
    uint32_t w = 0;

    const char* hello = "DISKEXE OK: winapp.exe loaded from VHD via kernel32\n";
    WriteFile(out, hello, slen(hello), &w, nullptr);

    char* buf = (char*)HeapAlloc(GetProcessHeap(), 0, 64);
    if (buf) {
        const char* m = "  + heap allocation works inside the disk binary\n";
        uint32_t i = 0;
        for (; m[i]; ++i) buf[i] = m[i];
        WriteFile(out, buf, i, &w, nullptr);
    }
    ExitProcess(0);
}
