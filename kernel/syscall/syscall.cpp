// syscall.cpp - MicroNT M6 SYSCALL setup and dispatch

#include "../include/hal.h"
#include "../include/debug.h"
#include "../include/process.h"
#include "../include/ntstatus.h"
#include "../include/memory.h"
#include "../include/io.h"

// ============================================================
// MSR addresses
// ============================================================
constexpr u32 MSR_EFER   = 0xC0000080;  // Extended Feature Enable Register
constexpr u32 MSR_STAR   = 0xC0000081;  // SYSCALL target CS/SS + SYSRET CS/SS
constexpr u32 MSR_LSTAR  = 0xC0000082;  // 64-bit SYSCALL target RIP
constexpr u32 MSR_SFMASK = 0xC0000084;  // RFLAGS bits to clear on SYSCALL

static u64 RdMsr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (u64)lo | ((u64)hi << 32);
}
static void WrMsr(u32 msr, u64 val) {
    __asm__ volatile("wrmsr" :: "c"(msr),
                               "a"((u32)(val & 0xFFFFFFFF)),
                               "d"((u32)(val >> 32)));
}

// ============================================================
// Syscall numbers (M6)
// ============================================================
constexpr u64 NT_TEST_SYSCALL     = 0;
constexpr u64 NT_TERMINATE_THREAD = 1;
constexpr u64 NT_WRITE_CONSOLE    = 2;
constexpr u64 NT_TEST_PE          = 3;
constexpr u64 NT_WRITE_FILE       = 4;
constexpr u64 NT_READ_LINE        = 5;
constexpr u64 NT_CREATE_FILE      = 6;
constexpr u64 NT_READ_FILE        = 7;
constexpr u64 NT_CLOSE_HANDLE     = 8;
constexpr u64 NT_QUERY_DIR        = 9;
constexpr u64 NT_ALLOC_VM         = 10;   // M11: NtAllocateVirtualMemory(size, protect)
constexpr u64 NT_FREE_VM          = 11;   // M11: NtFreeVirtualMemory(base) - stub

// Command queue (pre-populated by kernel_main for M9 test)
static const char* s_cmds[8] = {};
static u32         s_cmd_count = 0;
static u32         s_cmd_idx   = 0;

// ============================================================
// Completion signal (checked by kernel_main M6 test)
// ============================================================
volatile u32 g_m6_syscall_ok = 0;
volatile u32 g_m7_pe_ok      = 0;
volatile u32 g_m8_write_ok   = 0;
volatile u32 g_m9_ver_ok     = 0;
volatile u32 g_m11_heap_ok   = 0;

// Per-process user heap: bump allocator starting at 0x50000000.
// Grows upward one PAGE_SIZE at a time.  Simple but correct for M11.
// Each process gets its own range because each has its own PML4.
constexpr u64 USER_HEAP_BASE = 0x500000000ULL;  // 20 GB: above 4GB identity map, no huge pages
constexpr u64 USER_HEAP_MAX  = 0x580000000ULL; // 22 GB ceiling (2 GB heap space)

// Returns current heap pointer, bumps it forward by 'pages' pages.
// Stored per-KThread in EntryArg (repurposed as heap_cursor for now).
// Actually simpler: use a global per-process cursor stored in KProcess.
// For M11 we just keep a global since we run one user process at a time.
static u64 s_user_heap_cursor = USER_HEAP_BASE;

// Write one byte to user memory via explicit PML4 walk
static bool WriteUserByte(u64 pml4, u64 user_va, u8 val) {
    u64 phys = VMM::TranslateInPml4(pml4, user_va);
    if (!phys) return false;
    *reinterpret_cast<u8*>(phys) = val;
    return true;
}

// Read up to 'len' bytes from user virtual address into kernel buffer.
// Uses the current thread's process Cr3 to walk the user PML4.
static usize ReadUserBytes(u64 user_va, u8* kbuf, usize len) {
    KThread* t = Sched::CurrentThread();
    if (!t || !t->Process) return 0;
    u64 pml4 = t->Process->Cr3;
    usize copied = 0;
    while (copied < len) {
        u64 phys = VMM::TranslateInPml4(pml4, user_va + copied);
        if (!phys) break;
        usize off  = (user_va + copied) & 0xFFF;
        usize avail = PAGE_SIZE - off;
        usize n = len - copied < avail ? len - copied : avail;
        const u8* src = reinterpret_cast<const u8*>(phys);
        for (usize i=0;i<n;++i) kbuf[copied+i] = src[i];
        copied += n;
    }
    return copied;
}

// Assembly entry point (syscall_entry.asm)
extern "C" void syscall_entry();

// ============================================================
// C-level dispatcher (called from syscall_entry.asm as KiSystemCall)
// ============================================================
extern "C" u64 KiSystemCall(u64 number, u64 a1, u64 a2,
                             u64 a3, u64 a4, u64 a5) {
    UNUSED(a2); UNUSED(a3); UNUSED(a4); UNUSED(a5);
    switch (number) {
    case NT_TEST_SYSCALL:
        KDBG_INFO("SYSCALL: NtTestSyscall(a1=0x%llx) - user mode reached kernel!", a1);
        g_m6_syscall_ok = 1;
        return (u64)STATUS_SUCCESS;

    case NT_TEST_PE:
        KDBG_INFO("SYSCALL: NtTestPe(marker=0x%llx) - PE loaded and executing!", a1);
        g_m7_pe_ok = static_cast<u32>(a1);  // store marker for kernel to check
        return (u64)STATUS_SUCCESS;

    case NT_READ_LINE: {
        // a1=user_buf_va, a2=max_len. Returns bytes written (0=no more commands).
        if (s_cmd_idx >= s_cmd_count) return 0;
        const char* cmd = s_cmds[s_cmd_idx++];
        usize len = 0; while (cmd[len]) ++len;
        if (len >= (usize)a2) len = (usize)a2 - 1;
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return 0;
        u64 pml4 = t->Process->Cr3;
        for (usize i=0; i<len; ++i) WriteUserByte(pml4, a1+i, (u8)cmd[i]);
        WriteUserByte(pml4, a1+len, 0);  // null terminate
        KDBG_TRACE("SYSCALL: NtReadLine -> '%s' (%llu bytes)", cmd, (u64)len);
        return (u64)len;
    }

    case NT_WRITE_FILE: {
        // a1=handle(unused for now), a2=user_buf_va, a3=length
        u64 user_va = a2;
        usize len   = (usize)(a3 > 512 ? 512 : a3);
        u8 kbuf[512+1] = {};
        usize got = ReadUserBytes(user_va, kbuf, len);
        if (got > 0) {
            kbuf[got] = '\0';
            Debug::Print("[USER] ");
            // Print bytes (may not be null-terminated)
            for (usize i=0; i<got; ++i) {
                char c[2]={static_cast<char>(kbuf[i]),0};
                Debug::Print(c);
            }
            g_m8_write_ok = 1;
            // M9: check for version string
            if (got >= 7 && kbuf[0]=='M' && kbuf[1]=='i' && kbuf[2]=='c')
                g_m9_ver_ok = 1;
        }
        return (u64)STATUS_SUCCESS;
    }
        KDBG_INFO("SYSCALL: NtTerminateThread(exit=%lld)", (i64)a1);
        PS::TerminateCurrentThread(static_cast<i32>(a1));
        // never returns
        return (u64)STATUS_SUCCESS;

    case NT_WRITE_CONSOLE:
        KDBG_TRACE("SYSCALL: NtWriteConsole stub");
        return (u64)STATUS_SUCCESS;

    case NT_ALLOC_VM: {
        // a1=size (bytes, rounded up to pages), a2=protect (PAGE_READWRITE=4 etc.)
        // Returns virtual address of allocated region, or 0 on failure.
        usize size = (usize)a1;
        if (size == 0) return 0;
        usize pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return 0;
        u64 pml4 = t->Process->Cr3;

        // Bump-allocate from user heap range
        u64 va = s_user_heap_cursor;
        if (va + pages * PAGE_SIZE > USER_HEAP_MAX) {
            KDBG_ERROR("SYSCALL: NtAllocVM: user heap exhausted");
            return 0;
        }
        s_user_heap_cursor += pages * PAGE_SIZE;

        // Map physical pages into the process PML4
        u64 flags = VMM::PTE_PRESENT | VMM::PTE_WRITABLE | VMM::PTE_USER;
        for (usize i = 0; i < pages; ++i) {
            u64 phys = PMM::AllocPage();
            if (!phys) { KDBG_ERROR("SYSCALL: NtAllocVM: PMM out"); return 0; }
            // Zero the page (identity map: phys == virt for < 4 GB)
            u8* p = reinterpret_cast<u8*>(phys);
            for (usize j = 0; j < PAGE_SIZE; ++j) p[j] = 0;
            if (!VMM::MapPageInto(pml4, va + i * PAGE_SIZE, phys, flags)) {
                KDBG_ERROR("SYSCALL: NtAllocVM: MapPageInto failed");
                return 0;
            }
        }
        KDBG_TRACE("SYSCALL: NtAllocVM(size=%llu) -> 0x%llx (%llu pages)", (u64)size, va, (u64)pages);
        g_m11_heap_ok = 1;
        return va;
    }

    case NT_FREE_VM:
        // Stub: no-op for M11 (bump allocator has no free)
        return (u64)STATUS_SUCCESS;

    case NT_TERMINATE_THREAD:
        KDBG_INFO("SYSCALL: NtTerminateThread(exit=%lld)", (i64)a1);
        PS::TerminateCurrentThread(static_cast<i32>(a1));
        return (u64)STATUS_SUCCESS;

    case NT_CREATE_FILE: {
        // a1=user_path_va, a2=path_len. Returns handle index+1 (>0), or 0 on fail.
        usize plen = (usize)(a2 > 127 ? 127 : a2);
        char kpath[128] = {};
        ReadUserBytes(a1, reinterpret_cast<u8*>(kpath), plen);
        kpath[plen] = '\0';
        KDBG_TRACE("SYSCALL: NtCreateFile('%s')", kpath);
        i32 h = VFS::Open(kpath);
        return h >= 0 ? (u64)(h + 1) : 0;   // encode: 0=fail, N+1=handle
    }

    case NT_READ_FILE: {
        // a1=handle(1-based), a2=user_buf_va, a3=offset, a4=len
        i32 h = (i32)((i64)a1 - 1);
        u64 offset = a3;
        usize len  = (usize)(a4 > 4096 ? 4096 : a4);
        u8 kbuf[4096] = {};
        i64 got = VFS::Read(h, offset, kbuf, len);
        if (got <= 0) return 0;
        // Copy to user buffer
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return 0;
        u64 pml4 = t->Process->Cr3;
        for (i64 i=0; i<got; ++i)
            WriteUserByte(pml4, a2+i, kbuf[i]);
        return (u64)got;
    }

    case NT_CLOSE_HANDLE: {
        // a1=handle(1-based)
        i32 h = (i32)((i64)a1 - 1);
        VFS::Close(h);
        return (u64)STATUS_SUCCESS;
    }

    case NT_QUERY_DIR: {
        // a1=user_buf_va, a2=buf_len
        // Writes null-separated filenames + sizes: "name\0" repeated, then "\0"
        usize buf_len = (usize)(a2 > 2048 ? 2048 : a2);
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return 0;
        u64 pml4 = t->Process->Cr3;
        u64 pos  = a1;
        u64 end  = a1 + buf_len - 1;

        // Callback writes each filename
        struct CbArgs { u64 pml4; u64 pos; u64 end; };
        static CbArgs cb_args;
        cb_args = { pml4, pos, end };
        VFS::ForEach([](const char* name, u64 /*size*/) {
            for (usize i = 0; name[i] && cb_args.pos < cb_args.end; ++i) {
                WriteUserByte(cb_args.pml4, cb_args.pos++, (u8)name[i]);
            }
            if (cb_args.pos < cb_args.end)
                WriteUserByte(cb_args.pml4, cb_args.pos++, 0);  // separator
        });
        // Final null terminator
        if (cb_args.pos < end) WriteUserByte(pml4, cb_args.pos++, 0);
        return cb_args.pos - a1;
    }

    default:
        KDBG_WARN("SYSCALL: unknown number %llu (a1=0x%llx)", number, a1);
        return (u64)STATUS_INVALID_SYSTEM_SERVICE;
    }
}

namespace SYSCALL {

void Init() {
    // Enable SYSCALL/SYSRET in EFER (SCE = bit 0)
    WrMsr(MSR_EFER, RdMsr(MSR_EFER) | 1u);

    // STAR:
    //   [63:48] = 0x10 -> SYSRET  CS=0x10+16|3=0x23(ucode) SS=0x10+8|3=0x1B(udata)
    //   [47:32] = 0x08 -> SYSCALL CS=0x08(kcode)            SS=0x10(kdata)
    WrMsr(MSR_STAR, (0x10ULL << 48) | (0x08ULL << 32));

    // LSTAR: kernel-mode syscall handler
    WrMsr(MSR_LSTAR, reinterpret_cast<u64>(syscall_entry));

    // SFMASK: RFLAGS bits to clear on SYSCALL entry
    //   IF (bit 9): disable interrupts until we switch to kernel stack
    //   TF (bit 8): no single-step tracing
    //   DF (bit 10): clear direction flag
    WrMsr(MSR_SFMASK, (1u << 9) | (1u << 8) | (1u << 10));

    KDBG_INFO("SYSCALL: LSTAR=0x%llx STAR=0x%llx",
              reinterpret_cast<u64>(syscall_entry),
              (0x10ULL << 48) | (0x08ULL << 32));
}

} // namespace SYSCALL

namespace SYSCALL {

void SetCommands(const char** cmds, u32 count) {
    s_cmd_idx = 0;
    s_cmd_count = 0;
    for (u32 i = 0; i < count && i < 8; ++i)
        s_cmds[s_cmd_count++] = cmds[i];
    KDBG_INFO("SYSCALL: command queue loaded (%u commands)", s_cmd_count);
}

} // namespace SYSCALL
