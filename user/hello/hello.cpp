// hello.cpp - MicroNT M7 user-mode test program
// No CRT, no imports. Uses direct syscall instruction.
// Built as PE32+ with lld-link /SUBSYSTEM:CONSOLE.
//
// Syscall numbers (must match kernel/syscall/syscall.cpp):
//   0 = NtTestSyscall(u64 value)
//   1 = NtTerminateThread(i32 exit_code)
//   2 = NtWriteConsole(const char* buf, u32 len)

static void nt_write(const char* buf, unsigned len) {
    __asm__ volatile(
        "mov $2, %%rax\n\t"   // NtWriteConsole
        "mov %0, %%rdi\n\t"
        "mov %1, %%rsi\n\t"
        "syscall"
        :: "r"((long long)buf), "r"((long long)len)
        : "rax","rdi","rsi","rcx","r11","memory"
    );
}

static void nt_exit(int code) {
    __asm__ volatile(
        "mov $1, %%rax\n\t"   // NtTerminateThread
        "mov %0, %%rdi\n\t"
        "syscall"
        :: "r"((long long)code)
        : "rax","rdi","rcx","r11","memory"
    );
}

// Simple strlen
static unsigned slen(const char* s) {
    unsigned n = 0; while (s[n]) ++n; return n;
}

extern "C" void EntryPoint() {
    const char* msg1 = "[HELLO] Hello from user mode!\n";
    const char* msg2 = "[HELLO] MicroNT M7: PE loader works.\n";
    nt_write(msg1, slen(msg1));
    nt_write(msg2, slen(msg2));
    nt_exit(42);
    for (;;) {}
}
