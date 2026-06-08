// wintest.cpp -- freestanding test EXE built by the user-mode PE pipeline
// (scripts/build-user.ps1: clang++ --target=x86_64-unknown-windows + lld-link).
//
// Proves the toolchain can compile real Windows-style user code into a PE32+
// that the MicroNT loader maps and runs. It talks to the kernel through the
// raw NT syscall ABI (no CRT, no imports yet).
#include <stdint.h>

using i64 = int64_t;
using u64 = uint64_t;

// MicroNT syscall ABI: number in RAX, args in RDI/RSI/RDX(/R10/R8), result RAX.
static inline i64 nt_syscall(i64 n, i64 a1, i64 a2, i64 a3) {
    i64 r;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "r8", "r9", "r10", "memory");
    return r;
}

enum { NT_TERMINATE_THREAD = 1, NT_WRITE_FILE = 4 };

extern "C" void Entry() {
    static const char msg[] = "WINPE OK from compiled PE\n";
    nt_syscall(NT_WRITE_FILE, 1 /*stdout*/, (i64)(u64)&msg[0], (i64)(sizeof(msg) - 1));
    nt_syscall(NT_TERMINATE_THREAD, 0, 0, 0);
    for (;;) {}
}
