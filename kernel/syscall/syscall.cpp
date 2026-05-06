// syscall.cpp - MicroNT M6 SYSCALL setup and dispatch

#include "../include/hal.h"
#include "../include/debug.h"
#include "../include/process.h"
#include "../include/ntstatus.h"
#include "../include/memory.h"
#include "../include/io.h"
#include "../include/sync.h"

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
constexpr u64 NT_ALLOC_VM         = 10;
constexpr u64 NT_FREE_VM          = 11;
constexpr u64 NT_CREATE_EVENT     = 12;
constexpr u64 NT_SET_EVENT        = 13;
constexpr u64 NT_WAIT_SINGLE      = 14;
constexpr u64 NT_RESET_EVENT      = 15;
constexpr u64 NT_CREATE_THREAD    = 16;
constexpr u64 NT_DELAY_EXECUTION  = 17;
constexpr u64 NT_QUERY_SYSINFO    = 18;
constexpr u64 NT_SET_THREAD_INFO  = 19;
constexpr u64 NT_CREATE_SECTION   = 20;
constexpr u64 NT_MAP_VIEW         = 21;
constexpr u64 NT_UNMAP_VIEW       = 22;
constexpr u64 NT_SET_EX_HANDLER   = 23;
constexpr u64 NT_RAISE_EXCEPTION  = 24;
constexpr u64 NT_CREATE_SEMAPHORE = 25;
constexpr u64 NT_RELEASE_SEMAPHORE= 26;
constexpr u64 NT_CREATE_MUTANT    = 27;
constexpr u64 NT_RELEASE_MUTANT   = 28;
constexpr u64 SEMA_HANDLE_BASE    = 0x100;
constexpr u64 SEMA_MAX            = 8;
constexpr u64 MUTANT_HANDLE_BASE  = 0x200;
constexpr u64 MUTANT_MAX          = 8;

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
volatile u32 g_m12_sync_ok   = 0;
volatile u32 g_m13_thread_ok = 0;
volatile u32 g_m14_info_ok   = 0;
volatile u32 g_m15_ok        = 0;
volatile u32 g_m16_ok        = 0;

// M15: exception delivery VA -- set by NT_RAISE_EXCEPTION, cleared by syscall_entry.asm
extern "C" volatile u64 g_pending_exception_va = 0;

// M15: shared memory section table
struct KSection {
    u64   phys[32];
    usize page_count;
    bool  in_use;
};
static KSection s_sections[8];

struct KSema {
    i32 count; i32 max_count;
    KThread* head; KThread* tail;
    bool in_use;
};
static KSema s_semas[8];

struct KMutant {
    KThread* owner; u32 depth;
    KThread* head; KThread* tail;
    bool in_use;
};
static KMutant s_mutants[8];

// Simple event handle table (handle = index+1)
constexpr usize EVENT_TABLE_SIZE = 32;
static KEvent* s_events[EVENT_TABLE_SIZE] = {};

// Per-process user heap: bump allocator starting at 0x50000000.
// Grows upward one PAGE_SIZE at a time.  Simple but correct for M11.
// Each process gets its own range because each has its own PML4.

// Returns current heap pointer, bumps it forward by 'pages' pages.
// Stored per-KThread in EntryArg (repurposed as heap_cursor for now).
// Actually simpler: use a global per-process cursor stored in KProcess.
// For M11 we just keep a global since we run one user process at a time.

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
            if (got >= 7 && kbuf[0]=='M' && kbuf[1]=='i' && kbuf[2]=='c')
                g_m9_ver_ok = 1;
            if (got >= 9 && kbuf[0]=='T' && kbuf[1]=='H' && kbuf[2]=='R')
                g_m13_thread_ok = 1;
            // M14: "Memory:" prefix
            if (got >= 7 && kbuf[0]=='M' && kbuf[3]=='o' && kbuf[5]=='y')
                g_m14_info_ok = 1;
            // M15: "M15 OK" prefix
            if (got >= 6 && kbuf[0]=='M' && kbuf[1]=='1' && kbuf[2]=='5')
                g_m15_ok = 1;
            if (got >= 6 && kbuf[0]=='M' && kbuf[1]=='1' && kbuf[2]=='6')
                g_m16_ok = 1;
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

    case NT_CREATE_EVENT: {
        // a1=auto_reset(0/1), a2=initially_signaled(0/1)
        // Returns event handle (1-based index), or 0 on failure
        KEvent* ev = SYNC::EventAlloc((bool)a1, (bool)a2);
        if (!ev) return 0;
        for (usize i = 0; i < EVENT_TABLE_SIZE; ++i) {
            if (!s_events[i]) {
                s_events[i] = ev;
                KDBG_TRACE("SYSCALL: NtCreateEvent(auto=%llu) -> handle %llu", a1, (u64)(i+1));
                return (u64)(i + 1);
            }
        }
        KDBG_ERROR("SYSCALL: NtCreateEvent: event table full");
        return 0;
    }

    case NT_SET_EVENT: {
        // a1=handle
        usize idx = (usize)(a1 - 1);
        if (idx >= EVENT_TABLE_SIZE || !s_events[idx]) return (u64)STATUS_INVALID_HANDLE;
        KDBG_TRACE("SYSCALL: NtSetEvent(handle=%llu)", a1);
        SYNC::EventSet(s_events[idx]);
        return (u64)STATUS_SUCCESS;
    }

    case NT_WAIT_SINGLE: {
        // Dispatch by handle range
        if (a1 >= MUTANT_HANDLE_BASE) {
            u64 i = a1 - MUTANT_HANDLE_BASE;
            if (i >= MUTANT_MAX || !s_mutants[i].in_use) return (u64)STATUS_INVALID_HANDLE;
            KMutant& m = s_mutants[i];
            KThread* cur = Sched::CurrentThread();
            HAL::DisableInterrupts();
            if (!m.owner) { m.owner=cur; m.depth=1; HAL::EnableInterrupts(); }
            else if (m.owner==cur) { ++m.depth; HAL::EnableInterrupts(); }
            else {
                cur->WaitNext=nullptr;
                if (m.tail) m.tail->WaitNext=cur; else m.head=cur; m.tail=cur;
                Sched::BlockCurrentThread(); HAL::EnableInterrupts(); Sched::Schedule();
            }
            return (u64)STATUS_SUCCESS;
        }
        if (a1 >= SEMA_HANDLE_BASE) {
            u64 i = a1 - SEMA_HANDLE_BASE;
            if (i >= SEMA_MAX || !s_semas[i].in_use) return (u64)STATUS_INVALID_HANDLE;
            KSema& s = s_semas[i];
            KThread* cur = Sched::CurrentThread();
            HAL::DisableInterrupts();
            if (s.count > 0) { --s.count; HAL::EnableInterrupts(); }
            else if (a2==0) { HAL::EnableInterrupts(); return (u64)STATUS_TIMEOUT; }
            else {
                cur->WaitNext=nullptr;
                if (s.tail) s.tail->WaitNext=cur; else s.head=cur; s.tail=cur;
                Sched::BlockCurrentThread(); HAL::EnableInterrupts(); Sched::Schedule();
            }
            return (u64)STATUS_SUCCESS;
        }
        // Fall through to event handling
        // a1=handle, a2=timeout_ms
        usize idx = (usize)(a1 - 1);
        if (idx >= EVENT_TABLE_SIZE || !s_events[idx]) return (u64)STATUS_INVALID_HANDLE;
        u32 timeout = (u32)(a2 & 0xFFFFFFFF);
        KDBG_TRACE("SYSCALL: NtWaitForSingleObject(handle=%llu timeout=%u)", a1, timeout);
        NTSTATUS st = SYNC::EventWait(s_events[idx], timeout);
        if (NT_SUCCESS(st)) g_m12_sync_ok = 1;
        return (u64)st;
    }

    case NT_RESET_EVENT: {
        // a1=handle
        usize idx = (usize)(a1 - 1);
        if (idx >= EVENT_TABLE_SIZE || !s_events[idx]) return (u64)STATUS_INVALID_HANDLE;
        KDBG_TRACE("SYSCALL: NtResetEvent(handle=%llu)", a1);
        SYNC::EventReset(s_events[idx]);
        return (u64)STATUS_SUCCESS;
    }

    case NT_CREATE_THREAD: {
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return 0;
        KProcess* proc = t->Process;
        constexpr usize THREAD_STACK_PAGES = 4;
        constexpr usize THREAD_STACK_SIZE  = THREAD_STACK_PAGES * PAGE_SIZE;

        constexpr u64 HEAP_MAX = 0x580000000ULL;
        if (proc->UserHeapCursor + THREAD_STACK_SIZE > HEAP_MAX) return 0;
        u64 stack_va = proc->UserHeapCursor;
        proc->UserHeapCursor += THREAD_STACK_SIZE;

        u64 pml4 = proc->Cr3;
        u64 stack_flags = VMM::PTE_PRESENT | VMM::PTE_WRITABLE | VMM::PTE_USER;
        for (usize i = 0; i < THREAD_STACK_PAGES; ++i) {
            u64 phys = PMM::AllocPage();
            if (!phys) return 0;
            u8* p = reinterpret_cast<u8*>(phys);
            for (usize j=0;j<PAGE_SIZE;++j) p[j]=0;
            if (!VMM::MapPageInto(pml4, stack_va + i*PAGE_SIZE, phys, stack_flags)) return 0;
        }
        u64 stack_top = stack_va + THREAD_STACK_SIZE;
        KThread* nt = PS::CreateUserThread(proc, "uthread", a1, stack_top, a2);
        if (!nt) return 0;
        Sched::AddThread(nt);
        KDBG_INFO("SYSCALL: NtCreateThread(entry=0x%llx arg=0x%llx) -> TID %u", a1, a2, nt->Tid);
        return (u64)nt->Tid;
    }

    case NT_QUERY_SYSINFO: {
        // a1=class, a2=user_buf_va, a3=buf_size
        // Returns bytes written, or 0 on error.
        // Class 0: kernel version string
        // Class 1: memory stats string
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return 0;
        u64 pml4 = t->Process->Cr3;
        usize maxlen = (usize)(a3 > 256 ? 256 : a3);

        const char* str = nullptr;
        char membuf[64] = {};

        if (a1 == 0) {
            str = "MicroNT Version M14\r\n";
        } else if (a1 == 1) {
            // Memory stats: approximate from PMM free pages
            u64 free_kb = (u64)PMM::FreePages() * (PAGE_SIZE / 1024);
            u64 total_kb = free_kb + (u64)PMM::UsedPages() * (PAGE_SIZE / 1024);
            // Simple itoa for KB values
            auto itoa_k = [](char* buf, u64 v) -> usize {
                if (v == 0) { buf[0]='0'; buf[1]='\0'; return 1; }
                usize i=0; char tmp[20]; usize n=0;
                while(v>0){tmp[n++]='0'+(v%10);v/=10;}
                while(n>0) buf[i++]=tmp[--n]; buf[i]='\0'; return i;
            };
            char* p = membuf;
            const char* pfx = "Memory: Free="; while(*pfx) *p++=*pfx++;
            p += itoa_k(p, free_kb);
            const char* mid = " KB Total="; while(*mid) *p++=*mid++;
            p += itoa_k(p, total_kb);
            const char* sfx = " KB\r\n"; while(*sfx) *p++=*sfx++;
            *p = '\0';
            str = membuf;
        }
        if (!str) return 0;

        usize len = 0; while (str[len]) ++len;
        if (len > maxlen) len = maxlen;

        for (usize i=0; i<len; ++i) WriteUserByte(pml4, a2+i, (u8)str[i]);
        WriteUserByte(pml4, a2+len, 0);
        return (u64)len;
    }

    case NT_DELAY_EXECUTION:
        // a1 = milliseconds to sleep
        if (a1 > 0) {
            KDBG_TRACE("SYSCALL: NtDelayExecution(%llu ms)", a1);
            Sched::Sleep((u32)a1);
        }
        return (u64)STATUS_SUCCESS;

    case NT_ALLOC_VM: {
        usize size = (usize)a1;
        if (size == 0) return 0;
        usize pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return 0;
        KProcess* proc = t->Process;
        u64 pml4 = proc->Cr3;

        // Per-process bump allocator (each process starts at 0x500000000)
        constexpr u64 HEAP_MAX = 0x580000000ULL;
        u64 va = proc->UserHeapCursor;
        if (va + pages * PAGE_SIZE > HEAP_MAX) {
            KDBG_ERROR("SYSCALL: NtAllocVM: per-process heap exhausted");
            return 0;
        }
        proc->UserHeapCursor += pages * PAGE_SIZE;

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

    case NT_SET_THREAD_INFO: {
        KThread* target = Sched::CurrentThread();
        if (!target) return (u64)STATUS_NOT_FOUND;
        if (a2 == 0) {
            u32 pri = (u32)a3;
            if (pri >= THREAD_PRIORITY_COUNT) return (u64)STATUS_INVALID_PARAMETER;
            target->Priority = pri;
            KDBG_TRACE("SYSCALL: NtSetInformationThread: priority -> %u", pri);
        }
        return (u64)STATUS_SUCCESS;
    }

    case NT_CREATE_SECTION: {
        usize sz    = (usize)a1;
        usize pages = (sz + PAGE_SIZE - 1) / PAGE_SIZE;
        if (!sz || pages > 32) return 0;
        for (usize i = 0; i < 8; ++i) {
            if (!s_sections[i].in_use) {
                s_sections[i].page_count = pages;
                s_sections[i].in_use     = true;
                for (usize j = 0; j < pages; ++j) {
                    u64 phys = PMM::AllocPage();
                    if (!phys) { s_sections[i].in_use = false; return 0; }
                    u8* p = reinterpret_cast<u8*>(phys);
                    for (usize k = 0; k < PAGE_SIZE; ++k) p[k] = 0;
                    s_sections[i].phys[j] = phys;
                }
                KDBG_TRACE("SYSCALL: NtCreateSection(sz=%llu) -> handle %llu", (u64)sz, (u64)(i+1));
                return (u64)(i + 1);
            }
        }
        return 0;
    }

    case NT_MAP_VIEW: {
        if (!a1 || a1 > 8) return 0;
        KSection& sec = s_sections[a1 - 1];
        if (!sec.in_use) return 0;
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return 0;
        KProcess* proc = t->Process;
        constexpr u64 HEAP_MAX = 0x580000000ULL;
        u64 va = proc->UserHeapCursor;
        if (va + sec.page_count * PAGE_SIZE > HEAP_MAX) return 0;
        proc->UserHeapCursor += sec.page_count * PAGE_SIZE;
        u64 flags = VMM::PTE_PRESENT | VMM::PTE_WRITABLE | VMM::PTE_USER;
        for (usize i = 0; i < sec.page_count; ++i) {
            if (!VMM::MapPageInto(proc->Cr3, va + i * PAGE_SIZE, sec.phys[i], flags)) return 0;
        }
        KDBG_TRACE("SYSCALL: NtMapViewOfSection(h=%llu) -> 0x%llx", a1, va);
        return va;
    }

    case NT_UNMAP_VIEW: {
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return (u64)STATUS_INVALID_PARAMETER;
        for (usize i = 0; i < 8; ++i) {
            KSection& sec = s_sections[i];
            if (!sec.in_use) continue;
            u64 phys = VMM::TranslateInPml4(t->Process->Cr3, a1);
            if (phys && phys == sec.phys[0]) {
                for (usize j = 0; j < sec.page_count; ++j)
                    VMM::UnmapPageFrom(t->Process->Cr3, a1 + j * PAGE_SIZE);
                return (u64)STATUS_SUCCESS;
            }
        }
        return (u64)STATUS_NOT_FOUND;
    }

    case NT_SET_EX_HANDLER: {
        KThread* t = Sched::CurrentThread();
        if (!t) return (u64)STATUS_NOT_FOUND;
        t->ExceptionHandler = a1;
        KDBG_TRACE("SYSCALL: NtSetExceptionHandler(va=0x%llx)", a1);
        return (u64)STATUS_SUCCESS;
    }

    case NT_CREATE_SEMAPHORE: {
        i32 init=(i32)(u32)a1, maxc=(i32)(u32)a2;
        if (init<0||maxc<=0||init>maxc) return 0;
        for (u64 i=0;i<SEMA_MAX;++i) if (!s_semas[i].in_use) {
            s_semas[i]={init,maxc,nullptr,nullptr,true};
            KDBG_TRACE("SYSCALL: NtCreateSemaphore(%d/%d) -> 0x%llx",init,maxc,SEMA_HANDLE_BASE+i);
            return SEMA_HANDLE_BASE+i;
        }
        return 0;
    }

    case NT_RELEASE_SEMAPHORE: {
        u64 i=a1-SEMA_HANDLE_BASE;
        if (a1<SEMA_HANDLE_BASE||i>=SEMA_MAX||!s_semas[i].in_use) return (u64)STATUS_INVALID_HANDLE;
        KSema& s=s_semas[i]; i32 rel=(i32)(u32)a2; if(rel<=0)rel=1;
        HAL::DisableInterrupts();
        while (rel>0&&s.head) {
            KThread* t=s.head; s.head=t->WaitNext; if(!s.head)s.tail=nullptr;
            t->WaitNext=nullptr; Sched::UnblockThread(t); --rel;
        }
        i32 prev=s.count; s.count=(s.count+rel>s.max_count)?s.max_count:(s.count+rel);
        HAL::EnableInterrupts();
        KDBG_TRACE("SYSCALL: NtReleaseSemaphore prev=%d new=%d",prev,s.count);
        return (u64)STATUS_SUCCESS;
    }

    case NT_CREATE_MUTANT: {
        for (u64 i=0;i<MUTANT_MAX;++i) if (!s_mutants[i].in_use) {
            s_mutants[i].owner  = a1 ? Sched::CurrentThread() : nullptr;
            s_mutants[i].depth  = a1 ? 1u : 0u;
            s_mutants[i].head   = s_mutants[i].tail = nullptr;
            s_mutants[i].in_use = true;
            KDBG_TRACE("SYSCALL: NtCreateMutant -> 0x%llx", MUTANT_HANDLE_BASE+i);
            return MUTANT_HANDLE_BASE+i;
        }
        return 0;
    }

    case NT_RELEASE_MUTANT: {
        u64 i=a1-MUTANT_HANDLE_BASE;
        if (a1<MUTANT_HANDLE_BASE||i>=MUTANT_MAX||!s_mutants[i].in_use) return (u64)STATUS_INVALID_HANDLE;
        KMutant& m=s_mutants[i]; KThread* cur=Sched::CurrentThread();
        if (m.owner!=cur) return (u64)STATUS_MUTANT_NOT_OWNED;
        HAL::DisableInterrupts();
        if (--m.depth==0) {
            if (m.head) {
                KThread* t=m.head; m.head=t->WaitNext; if(!m.head)m.tail=nullptr;
                t->WaitNext=nullptr; m.owner=t; m.depth=1; Sched::UnblockThread(t);
            } else { m.owner=nullptr; }
        }
        HAL::EnableInterrupts();
        KDBG_TRACE("SYSCALL: NtReleaseMutant depth=%u",m.depth);
        return (u64)STATUS_SUCCESS;
    }

    case NT_RAISE_EXCEPTION: {
        KThread* t = Sched::CurrentThread();
        if (!t || !t->ExceptionHandler) {
            KDBG_ERROR("SYSCALL: NtRaiseException: no handler registered");
            return (u64)STATUS_NOT_FOUND;
        }
        g_pending_exception_va = t->ExceptionHandler;
        KDBG_TRACE("SYSCALL: NtRaiseException(code=0x%llx) -> handler 0x%llx",
                   a1, t->ExceptionHandler);
        return a1;  // becomes RAX in the handler
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
