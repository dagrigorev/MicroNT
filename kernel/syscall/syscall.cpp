// syscall.cpp - MicroNT M6 SYSCALL setup and dispatch

#include "../include/hal.h"
#include "../include/pe.h"
#include "../ldr/ntdll_pe.h"
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
constexpr u64 NT_CREATE_PROCESS   = 29;
constexpr u64 NT_WAIT_MULTI       = 30;
constexpr u64 NT_OPEN_EVENT       = 31;
constexpr u64 NT_VGA_CLEAR        = 32;
constexpr u64 PROC_HANDLE_BASE    = 0x300;
constexpr u64 PROC_TABLE_SIZE     = 8;
constexpr u64 SEMA_HANDLE_BASE    = 0x100;
constexpr u64 SEMA_MAX            = 8;
constexpr u64 MUTANT_HANDLE_BASE  = 0x200;

// M22: named pipe table
struct KPipe { char name[32]; u8 data[512]; u32 size; bool used; };
static KPipe g_pipes[4];
static constexpr u64 PIPE_HANDLE_BASE = 0x80;
constexpr u64 NT_CREATE_NAMED_PIPE = 33;

// Windows NT information queries (leverage the TEB/PEB/KUSER environment).
constexpr u64 NT_QUERY_PROCESS_INFO = 34;  // NtQueryInformationProcess
constexpr u64 NT_QUERY_THREAD_INFO  = 35;  // NtQueryInformationThread
constexpr u64 NT_QUERY_SYSTEM_TIME  = 36;  // NtQuerySystemTime
constexpr u64 NT_QUERY_PERF_COUNTER = 37;  // NtQueryPerformanceCounter
constexpr u64 NT_CREATE_FILE_SECTION = 38; // NtCreateSection over a VFS file
constexpr u64 NT_PROTECT_VM          = 39; // NtProtectVirtualMemory
constexpr u64 NT_QUERY_VM            = 40; // NtQueryVirtualMemory

// M30: writable files delegated to VFS::WNode table.
// Handle range 0x40-0x4F maps to VFS WNode indices 0-15.
static constexpr u64 WFILE_HANDLE_BASE = 0x40;
static constexpr u64 WFILE_HANDLE_MAX  = 0x50;

static u64 WFindOrCreate(const char* name) {
    u32 idx = VFS::WFindOrCreate(name, false);
    return idx < 16 ? (u64)idx : 0xFF;
}

constexpr u64 MUTANT_MAX = 8;

// Command queue (pre-populated by kernel_main for automated tests)
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
volatile u32 g_m17_ok        = 0;
volatile u32 g_m18_ok        = 0;
volatile u32 g_m19_ok        = 0;
volatile u32 g_m20_ok        = 0;
volatile u32 g_m21_ok        = 0;
volatile u32 g_m22_ok        = 0;

// M15: exception delivery VA -- set by NT_RAISE_EXCEPTION, cleared by syscall_entry.asm
extern "C" volatile u64 g_pending_exception_va = 0;

// Set by the IDT when a hardware fault is delivered to a user SEH handler.
extern "C" volatile u32 g_seh_delivered = 0;

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
static KProcess* s_proc_table[PROC_TABLE_SIZE];  // M17 process handles

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

// Read one byte from user virtual address (explicit pml4)
static u8 ReadUserByte(u64 pml4, u64 va) {
    u64 phys = VMM::TranslateInPml4(pml4, va);
    return phys ? *reinterpret_cast<u8*>(phys) : 0;
}

// Write one byte to user memory via explicit PML4 walk
static bool WriteUserByte(u64 pml4, u64 user_va, u8 val) {
    u64 phys = VMM::TranslateInPml4(pml4, user_va);
    if (!phys) return false;
    *reinterpret_cast<u8*>(phys) = val;
    return true;
}

// Write a little-endian u64 to user memory via explicit PML4 walk.
static void WriteUserU64(u64 pml4, u64 user_va, u64 v) {
    for (int i = 0; i < 8; ++i) WriteUserByte(pml4, user_va + i, (u8)(v >> (i * 8)));
}

static void WriteUserU32(u64 pml4, u64 user_va, u32 v) {
    for (int i = 0; i < 4; ++i) WriteUserByte(pml4, user_va + i, (u8)(v >> (i * 8)));
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

// Create a section object backed by a copy of `data` (file-backed mapping).
// Returns a 1-based section handle, or 0 on failure.
static u64 CreateSectionFromData(const u8* data, usize size) {
    usize pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (!size || pages > 32) return 0;
    for (usize i = 0; i < 8; ++i) {
        if (s_sections[i].in_use) continue;
        s_sections[i].page_count = pages;
        s_sections[i].in_use     = true;
        for (usize j = 0; j < pages; ++j) {
            u64 phys = PMM::AllocPage();
            if (!phys) { s_sections[i].in_use = false; return 0; }
            u8* p = reinterpret_cast<u8*>(phys);
            usize off = j * PAGE_SIZE;
            for (usize k = 0; k < PAGE_SIZE; ++k)
                p[k] = (off + k < size) ? data[off + k] : 0;
            s_sections[i].phys[j] = phys;
        }
        return (u64)(i + 1);
    }
    return 0;
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
        // a1=user_buf_va, a2=max_len. Returns bytes written (0=end or error).
        // Pre-programmed command queue takes priority; keyboard fills in when empty.
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return 0;
        u64 pml4 = t->Process->Cr3;

        if (s_cmd_idx < s_cmd_count) {
            // Serve from pre-programmed queue
            const char* cmd = s_cmds[s_cmd_idx++];
            usize len = 0; while (cmd[len]) ++len;
            if (len >= (usize)a2) len = (usize)a2 - 1;
            for (usize i=0;i<len;++i) WriteUserByte(pml4, a1+i, (u8)cmd[i]);
            WriteUserByte(pml4, a1+len, 0);
            KDBG_TRACE("SYSCALL: NtReadLine (queue) -> '%s'", cmd);
            return (u64)len;
        }

        // ----------------------------------------------------------------
        // ----------------------------------------------------------------
        // M32: helpers used by kernel command handlers below
        // ----------------------------------------------------------------
        auto kprint = [](const char* s, u8 color) {
            for (; *s; ++s) VGA::PutChar(*s, color);
            VGA::PutChar('\n', 0x07);
        };
        auto kstrcmp = [](const char* a, const char* b, usize len) {
            for (usize i = 0; i < len; ++i) if (a[i]!=b[i]) return false;
            return true;
        };
        auto pfmt = [](char* buf, u64 v, u32 width) {
            char tmp[20]; u32 n=0;
            if (!v) { tmp[n++]='0'; }
            else { u64 x=v; while(x){tmp[n++]='0'+(u8)(x%10);x/=10;} }
            u32 pad = n<width ? width-n : 0;
            u32 i=0;
            while(pad--) buf[i++]=' ';
            while(n)     buf[i++]=tmp[--n];
            buf[i]=0;
        };
        auto DrawPsTable = [&]() {
            const char* hdr = "   PID  Name                 State     Thds  Heap KB";
            for (const char* p=hdr; *p; ++p) VGA::PutChar(*p, 0x0B);
            VGA::PutChar('\n', 0x07);
            const char* sep = "   -------------------------------------------------------";
            for (const char* p=sep; *p; ++p) VGA::PutChar(*p, 0x08);
            VGA::PutChar('\n', 0x07);
            char numbuf[16];
            for (u32 pi=0; pi<PS::ProcessCount(); ++pi) {
                KProcess* proc = PS::GetProcess(pi);
                if (!proc) continue;
                pfmt(numbuf, proc->Pid, 5);
                for (const char* p=numbuf; *p; ++p) VGA::PutChar(*p, 0x0A); // PID green
                VGA::PutChar(' ', 0x07); VGA::PutChar(' ', 0x07);
                u32 nlen=0;
                for (; proc->Name[nlen]&&nlen<20; ++nlen) VGA::PutChar(proc->Name[nlen], 0x0F);
                for (; nlen<20; ++nlen) VGA::PutChar(' ', 0x07);
                VGA::PutChar(' ', 0x07);
                const char* state="READY    "; u8 scol=0x0E;
                if (proc->exited)              { state="EXITED   "; scol=0x08; }
                else if (proc->thread_count>0) { state="RUNNING  "; scol=0x0A; }
                for (const char* p=state; *p; ++p) VGA::PutChar(*p, scol);
                pfmt(numbuf, proc->thread_count, 4);
                for (const char* p=numbuf; *p; ++p) VGA::PutChar(*p, 0x0E); // threads yellow
                constexpr u64 HEAP_BASE=0x500000000ULL;
                u64 hkb = proc->UserHeapCursor>HEAP_BASE
                          ? (proc->UserHeapCursor-HEAP_BASE)/1024 : 0;
                pfmt(numbuf, hkb, 8);
                for (const char* p=numbuf; *p; ++p) VGA::PutChar(*p, 0x0B); // heap cyan
                VGA::PutChar('\n', 0x07);
            }
        };

        // M26/M27: keyboard line editor with history and tab completion
        // ----------------------------------------------------------------
        // Special key codes from keyboard.cpp (C0 range)
        static constexpr char KEY_UP    = '\x10';
        static constexpr char KEY_DOWN  = '\x11';

        // -- Command history (M26) ----------------------------------------
        static constexpr u32 HIST_N   = 16;
        static constexpr u32 HIST_MAX = 256;
        static char  s_hist[HIST_N][HIST_MAX];
        static u32   s_hist_count = 0;
        static u32   s_hist_head  = 0;  // next write slot (ring)

        auto HistAdd = [&](const char* buf, usize len) {
            if (len == 0) return;
            // Skip if identical to the most recent entry
            if (s_hist_count > 0) {
                u32 prev = (s_hist_head + HIST_N - 1) % HIST_N;
                bool same = true;
                for (usize i = 0; i <= len && same; ++i)
                    same = (s_hist[prev][i] == (i < len ? buf[i] : '\0'));
                if (same) return;
            }
            usize n = len < HIST_MAX - 1 ? len : HIST_MAX - 1;
            for (usize i = 0; i < n;  ++i) s_hist[s_hist_head][i] = buf[i];
            s_hist[s_hist_head][n] = '\0';
            s_hist_head = (s_hist_head + 1) % HIST_N;
            if (s_hist_count < HIST_N) ++s_hist_count;
        };

        // back=1 -> most recent, back=2 -> second most recent, etc.
        auto HistGet = [&](u32 back) -> const char* {
            if (back == 0 || back > s_hist_count) return nullptr;
            return s_hist[(s_hist_head + HIST_N - back) % HIST_N];
        };

        // -- Tab completion (M27) -----------------------------------------
        static const char* const s_cmds[] = {
            "ver","dir","mem","ps","exec","cat","echo",
            "write","help","exit","clear","pipe", nullptr
        };

        auto TabComplete = [&](const char* buf, usize n) -> const char* {
            const char* match = nullptr;
            for (u32 i = 0; s_cmds[i]; ++i) {
                const char* cmd = s_cmds[i];
                bool ok = true;
                for (usize j = 0; j < n && ok; ++j) ok = (cmd[j] == buf[j]);
                if (!ok || cmd[n] == '\0') continue;  // no match or exact
                if (match) return nullptr;             // ambiguous
                match = cmd;
            }
            return match;
        };

        // -- Line editor loop ---------------------------------------------
        char   linebuf[512] = {};
        usize  n       = 0;
        i32    hist_pos = 0;  // 0 = current draft, 1+ = history depth
        usize  maxlen  = (usize)(a2 > 1 ? a2 - 1 : 0);

        // Prompt is printed by the shell via NtWriteFile("> ") which PrintUser
        // intercepts and renders as "[USER] > " with the cursor held on that row.
        VGA::UpdateCursor();

        while (n < maxlen) {
            char ch = 0;
            while (!KB::TryRead(&ch)) { Sched::Sleep(10); }

            // Enter
            if (ch == '\n' || ch == '\r') {
                VGA::PutChar('\n', 0x07);
                break;
            }

            // Backspace
            if (ch == '\b') {
                if (n > 0) { --n; VGA::PutChar('\b', 0x0F); }
                continue;
            }

            // Tab completion
            if (ch == '\t') {
                const char* comp = TabComplete(linebuf, n);
                if (comp) {
                    while (comp[n] && n < maxlen) {
                        VGA::PutChar(comp[n], 0x0F);
                        linebuf[n] = comp[n];
                        ++n;
                    }
                }
                continue;
            }

            // Arrow keys (history navigation)
            if (ch == KEY_UP || ch == KEY_DOWN) {
                i32 new_pos = hist_pos + (ch == KEY_UP ? 1 : -1);
                if (new_pos < 0) new_pos = 0;
                const char* entry = (new_pos == 0) ? nullptr : HistGet((u32)new_pos);
                if (new_pos == 0 || entry) {
                    // Erase current displayed line
                    while (n > 0) { --n; VGA::PutChar('\b', 0x0F); }
                    // Write history entry
                    if (entry) {
                        for (usize i = 0; entry[i] && n < maxlen; ++i) {
                            linebuf[n] = entry[i];
                            VGA::PutChar(entry[i], 0x0F);
                            ++n;
                        }
                    }
                    hist_pos = new_pos;
                }
                continue;
            }

            // Printable character
            linebuf[n++] = ch;
            VGA::PutChar(ch, 0x0F);
        }

        // Save to history and write to user buffer
        linebuf[n] = '\0';
        HistAdd(linebuf, n);

        // ---- M30/M32: kernel-handled commands ----
        // Return 0 (empty line) -> shell loops to next prompt without processing.

        // ps -- visual process table (M32 override of shell's ps)
        if (n == 2 && kstrcmp(linebuf, "ps", 2)) {
            VGA::PutChar('\n', 0x07);
            DrawPsTable();
            WriteUserByte(pml4, a1, 0); return 0;
        }
        // kill <pid>
        if (n > 5 && kstrcmp(linebuf, "kill ", 5)) {
            u32 pid = 0;
            for (usize i = 5; i < n && linebuf[i]>='0' && linebuf[i]<='9'; ++i)
                pid = pid*10 + (u32)(linebuf[i]-'0');
            if (PS::KillProcess(pid)) kprint("Process killed.", 0x0A);
            else                      kprint("PID not found or already exited.", 0x0C);
            WriteUserByte(pml4, a1, 0); return 0;
        }
        // bg <filename> -- spawn background process without waiting
        if (n > 3 && kstrcmp(linebuf, "bg ", 3)) {
            char bgname[64] = {};
            usize bglen = n - 3 < 63 ? n - 3 : 63;
            for (usize i = 0; i < bglen; ++i) bgname[i] = linebuf[3 + i];
            usize fsize = 0;
            const u8* fdata = VFS::FindFile(bgname, &fsize);
            if (!fdata || !fsize) {
                kprint("File not found.", 0x0C);
            } else {
                u64 ccr3 = VMM::CreateUserPml4();
                KProcess* cp = ccr3 ? PS::CreateProcess(bgname, ccr3) : nullptr;
                if (cp) {
                    u64 image_base = 0x500000000ULL, entry_va = 0;
                    LDR::LoadPe(fdata, fsize, ccr3, image_base, &entry_va);
                    if (entry_va) {
                        u64 stk = image_base + 0x100000ULL;
                        constexpr usize CSTK=4;
                        u64 fl=VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER;
                        for (usize i=0;i<CSTK;++i){
                            u64 pp=PMM::AllocPage(); if(!pp)break;
                            u8* pb=(u8*)pp; for(usize j=0;j<PAGE_SIZE;++j)pb[j]=0;
                            VMM::MapPageInto(ccr3,stk+i*PAGE_SIZE,pp,fl);
                        }
                        KThread* ct2=PS::CreateUserThread(cp,bgname,entry_va,stk+CSTK*PAGE_SIZE);
                        if (ct2) { Sched::AddThread(ct2); kprint("Background process started.", 0x0A); }
                        else kprint("Thread creation failed.", 0x0C);
                    } else kprint("PE load failed.", 0x0C);
                } else kprint("Process creation failed.", 0x0C);
            }
            WriteUserByte(pml4, a1, 0); return 0;
        }
        // mkdir <name>
        if (n > 6 && kstrcmp(linebuf, "mkdir ", 6)) {
            const char* dirname = linebuf + 6;
            if (VFS::MkDir(dirname)) kprint("Directory created.", 0x0A);
            else                     kprint("Already exists or table full.", 0x0C);
            WriteUserByte(pml4, a1, 0); return 0;
        }
        // rm <name>
        if (n > 3 && kstrcmp(linebuf, "rm ", 3)) {
            const char* fname = linebuf + 3;
            if (VFS::Delete(fname)) kprint("Deleted.", 0x0A);
            else                    kprint("Not found.", 0x0C);
            WriteUserByte(pml4, a1, 0); return 0;
        }
        // save -- persist VFS to NVRAM
        if (n == 4 && kstrcmp(linebuf, "save", 4)) {
            VFS::SaveNVRAM();
            kprint("VFS saved to NVRAM.", 0x0A);
            WriteUserByte(pml4, a1, 0); return 0;
        }
        // exit -- persist before shell exits
        if (n >= 4 && kstrcmp(linebuf, "exit", 4))
            VFS::SaveNVRAM();

        for (usize i = 0; i <= n; ++i) WriteUserByte(pml4, a1 + i, (u8)linebuf[i]);
        return (u64)n;
    }

    case NT_WRITE_FILE: {
        // a1=handle, a2=user_buf_va, a3=length
        // M21: if handle is a writable file handle, write to WFile table
        if (a1 >= PIPE_HANDLE_BASE && a1 < PIPE_HANDLE_BASE+4) {
            KPipe& kp = g_pipes[a1 - PIPE_HANDLE_BASE];
            if (!kp.used) return 0;
            KThread* tp = Sched::CurrentThread();
            if (!tp || !tp->Process) return 0;
            u64 pml4p = tp->Process->Cr3;
            usize wlen = (usize)(a3 > 512 ? 512 : a3);
            usize written = 0;
            while (written < wlen && kp.size < 512) {
                kp.data[kp.size++] = ReadUserByte(pml4p, a2 + written);
                ++written;
            }
            return (u64)written;
        }
        if (a1 >= WFILE_HANDLE_BASE && a1 < WFILE_HANDLE_MAX) {
            u32 widx = (u32)(a1 - WFILE_HANDLE_BASE);
            KThread* t2 = Sched::CurrentThread();
            if (!t2 || !t2->Process) return 0;
            u64 pml4w = t2->Process->Cr3;
            usize wlen = (usize)(a3 > 2048 ? 2048 : a3);
            u8 wbuf[2048];
            for (usize i = 0; i < wlen; ++i) wbuf[i] = ReadUserByte(pml4w, a2 + i);
            return (u64)VFS::WWrite(widx, wbuf, (u32)wlen);
        }
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
            if (got >= 6 && kbuf[0]=='M' && kbuf[1]=='1' && kbuf[2]=='7')
                g_m17_ok = 1;
            if ((got >= 6 && kbuf[0]=='M' && kbuf[1]=='1' && kbuf[2]=='8') ||
                (got >= 7 && kbuf[0]=='G' && kbuf[1]=='o' && kbuf[2]=='o' && kbuf[3]=='d'))
                g_m18_ok = 1;  // 'M18 OK' (old) or 'Goodbye' (interactive exit)
            // M19: ps output starts with "System"
            if (got >= 6 && kbuf[0]=='S' && kbuf[1]=='y' && kbuf[2]=='s' && kbuf[3]=='t')
                g_m19_ok = 1;
            // M20: echo command output starts with "M20"
            if (got >= 3 && kbuf[0]=='M' && kbuf[1]=='2' && kbuf[2]=='0') {
                if (!g_m20_ok) Debug::Print("\r\n[MicroNT] M20 ready\r\n");
                g_m20_ok = 1;
            }
            // M21: cat output echoes back written file with "M21" prefix
            if (got >= 3 && kbuf[0]=='M' && kbuf[1]=='2' && kbuf[2]=='1')
                g_m21_ok = 1;
            // M22: consumer.exe prints pipe content starting with "M22"
            if (got >= 3 && kbuf[0]=='M' && kbuf[1]=='2' && kbuf[2]=='2')
                g_m22_ok = 1;
            // Mirror output to VGA console (plain text, no prefix)
            VGA::PrintUser(reinterpret_cast<const char*>(kbuf), (usize)got);
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
        if (a1 >= PROC_HANDLE_BASE) {
            u64 i = a1 - PROC_HANDLE_BASE;
            if (i >= PROC_TABLE_SIZE || !s_proc_table[i]) return (u64)STATUS_INVALID_HANDLE;
            KProcess* proc = s_proc_table[i];
            if (proc->exited) return (u64)STATUS_SUCCESS;
            if (a2 == 0) return (u64)STATUS_TIMEOUT;
            // Block until process exits
            KThread* cur = Sched::CurrentThread();
            HAL::DisableInterrupts();
            cur->WaitNext = proc->exit_waiters;
            proc->exit_waiters = cur;
            Sched::BlockCurrentThread();
            HAL::EnableInterrupts();
            Sched::Schedule();
            return (u64)STATUS_SUCCESS;
        }
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
        char membuf[128] = {};

        if (a1 == 0) {
            // Read the OS version from KUSER_SHARED_DATA (0x7FFE0000) in THIS
            // process's address space -- proving the Windows-compatible shared
            // page is mapped per-process. Offsets are the documented x64 ones.
            volatile u32* sud = reinterpret_cast<volatile u32*>(0x7FFE0000ULL);
            u32 maj = sud[0x26C / 4];
            u32 min = sud[0x270 / 4];
            u32 bld = sud[0x274 / 4];
            char* p = membuf;
            auto app = [&](const char* s) { while (*s) *p++ = *s++; };
            auto num = [&](u32 v) {
                char t[12]; int n = 0;
                if (!v) t[n++] = '0'; else { while (v) { t[n++] = '0' + v % 10; v /= 10; } }
                while (n) *p++ = t[--n];
            };
            app("MicroNT [Windows ");
            app((maj == 10 && min == 0) ? "11" : "NT");
            app("] "); num(maj); *p++ = '.'; num(min); *p++ = '.'; num(bld);
            app("\r\n"); *p = 0;
            str = membuf;

            // End-to-end check: this syscall runs with the calling user
            // thread's GS base (no SWAPGS), so GS:[0x30]=TEB.Self and
            // GS:[0x60]=PEB. Read the PEB's OSBuildNumber back through GS to
            // prove the TEB/PEB/GS chain is live for a real user thread.
            u64 gsbase = HAL::ReadMsr(0xC0000101);
            if (gsbase) {
                u64 teb_self = 0, peb_ptr = 0;
                __asm__ volatile("movq %%gs:0x30, %0" : "=r"(teb_self));
                __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb_ptr));
                u32 peb_build = peb_ptr
                    ? *reinterpret_cast<volatile u16*>(peb_ptr + 0x120) : 0;
                Debug::Printf("[TEB/PEB] gs=0x%llx TEB.Self=0x%llx PEB=0x%llx "
                              "OSBuild=%u\r\n",
                              gsbase, teb_self, peb_ptr, peb_build);

                // Thread runtime: TLS pointer (TEB+0x58) + ProcessHeap (PEB+0x30).
                u64 tls = 0;
                __asm__ volatile("movq %%gs:0x58, %0" : "=r"(tls));
                u64 heap = peb_ptr
                    ? *reinterpret_cast<volatile u64*>(peb_ptr + 0x30) : 0;
                Debug::Printf("[RUNTIME] TLS=0x%llx ProcessHeap=0x%llx\r\n", tls, heap);
            }

            // Verify the NtQueryInformation* data for this live user thread.
            Debug::Printf("[NTINFO] ProcessBasicInfo PEB=0x%llx PID=%u | "
                          "ThreadBasicInfo TEB=0x%llx CID=%u.%u\r\n",
                          t->Process->PebVa, t->Process->Pid,
                          t->TebVa, t->Process->Pid, t->Tid);

            // Walk PEB->Ldr->InLoadOrderModuleList in the process's own address
            // space to prove the loaded-module list is well-formed and named.
            u64 peb_va = t->Process->PebVa;
            if (peb_va) {
                u64 ldr = *reinterpret_cast<volatile u64*>(peb_va + 0x18);
                u64 head = ldr + 0x10;
                u64 first = ldr ? *reinterpret_cast<volatile u64*>(head) : 0;
                if (first && first != head) {
                    u64 dllbase = *reinterpret_cast<volatile u64*>(first + 0x30);
                    u16 wlen = *reinterpret_cast<volatile u16*>(first + 0x58);
                    u64 nbuf = *reinterpret_cast<volatile u64*>(first + 0x60);
                    char nm[24]; u32 c = 0;
                    for (; nbuf && c < (u32)(wlen / 2) && c < 23; ++c)
                        nm[c] = (char)*reinterpret_cast<volatile u16*>(nbuf + c * 2);
                    nm[c] = 0;
                    Debug::Printf("[PEB.Ldr] InLoadOrder module='%s' DllBase=0x%llx\r\n",
                                  nm, dllbase);
                }
                // PEB->ProcessParameters->CommandLine (GetCommandLineW source).
                u64 params = *reinterpret_cast<volatile u64*>(peb_va + 0x20);
                if (params) {
                    u16 cl = *reinterpret_cast<volatile u16*>(params + 0x70);
                    u64 cb = *reinterpret_cast<volatile u64*>(params + 0x78);
                    char cmd[40]; u32 c = 0;
                    for (; cb && c < (u32)(cl / 2) && c < 39; ++c)
                        cmd[c] = (char)*reinterpret_cast<volatile u16*>(cb + c * 2);
                    cmd[c] = 0;
                    Debug::Printf("[PEB.Params] CommandLine='%s'\r\n", cmd);
                }
            }
        } else if (a1 == 1) {
            // Memory stats: provide enough data for visual bar rendering
            u64 free_pages  = (u64)PMM::FreePages();
            u64 used_pages  = (u64)PMM::UsedPages();
            u64 total_pages = (u64)PMM::TotalPages();
            u64 free_kb     = free_pages  * (PAGE_SIZE / 1024);
            u64 used_kb     = used_pages  * (PAGE_SIZE / 1024);
            u64 total_kb    = total_pages * (PAGE_SIZE / 1024);
            auto itoa_k = [](char* buf, u64 v) -> usize {
                if (v == 0) { buf[0]='0'; buf[1]='\0'; return 1; }
                char tmp[20]; usize n=0;
                while(v>0){tmp[n++]='0'+(v%10);v/=10;}
                usize i=0; while(n>0) buf[i++]=tmp[--n]; buf[i]='\0'; return i;
            };
            char* p = membuf;
            // Format: "Memory: Free=X KB Used=Y KB Total=Z KB Pages=A/B\r\n"
            auto app = [&](const char* s) { while(*s) *p++=*s++; };
            app("Memory: Free=");  p += itoa_k(p, free_kb);
            app(" KB Used=");      p += itoa_k(p, used_kb);
            app(" KB Total=");     p += itoa_k(p, total_kb);
            app(" KB Pages=");     p += itoa_k(p, used_pages);
            app("/");              p += itoa_k(p, total_pages);
            app("\r\n");           *p = '\0';
            str = membuf;
        }
        if (a1 == 2) {
            // Process list: "name (pid)\n" for each non-exited process
            usize total = 0;
            for (u32 pi = 0; pi < PS::ProcessCount() && total + 64 < maxlen; ++pi) {
                KProcess* proc = PS::GetProcess(pi);
                if (!proc) continue;  // show all processes including exited
                char line[64];
                usize j = 0;
                const char* nm = proc->Name;
                while (nm[j] && j < 28) { line[j] = nm[j]; j++; }
                line[j++] = ' '; line[j++] = '(';
                u32 pid = proc->Pid; char pb[8]; usize pn=0;
                if (!pid) { pb[pn++]='0'; }
                else { u32 v=pid; while(v){pb[pn++]='0'+(v%10);v/=10;} }
                while(pn>0) line[j++]=pb[--pn];
                line[j++]=')'; line[j++]='\n'; line[j]=0;
                for(usize k=0;k<j;++k) WriteUserByte(pml4,a2+total+k,(u8)line[k]);
                total += j;
            }
            if (total < maxlen) WriteUserByte(pml4, a2+total, 0);
            return (u64)total;
        }

        if (!str) return 0;

        usize len = 0; while (str[len]) ++len;
        if (len > maxlen) len = maxlen;

        for (usize i=0; i<len; ++i) WriteUserByte(pml4, a2+i, (u8)str[i]);
        WriteUserByte(pml4, a2+len, 0);
        return (u64)len;
    }

    case NT_QUERY_PROCESS_INFO: {
        // NtQueryInformationProcess. a1=class, a2=buf, a3=len.
        // Class 0 = ProcessBasicInformation (PROCESS_BASIC_INFORMATION, x64).
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return STATUS_UNSUCCESSFUL;
        u64 pml4 = t->Process->Cr3;
        if (a1 == 0 && a3 >= 48) {
            for (u32 i = 0; i < 48; ++i) WriteUserByte(pml4, a2 + i, 0);
            WriteUserU64(pml4, a2 + 0x08, t->Process->PebVa);  // PebBaseAddress
            WriteUserU64(pml4, a2 + 0x20, t->Process->Pid);    // UniqueProcessId
            return STATUS_SUCCESS;
        }
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    case NT_QUERY_THREAD_INFO: {
        // NtQueryInformationThread. a1=class, a2=buf, a3=len.
        // Class 0 = ThreadBasicInformation (THREAD_BASIC_INFORMATION, x64).
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return STATUS_UNSUCCESSFUL;
        u64 pml4 = t->Process->Cr3;
        if (a1 == 0 && a3 >= 48) {
            for (u32 i = 0; i < 48; ++i) WriteUserByte(pml4, a2 + i, 0);
            WriteUserU64(pml4, a2 + 0x08, t->TebVa);           // TebBaseAddress
            WriteUserU64(pml4, a2 + 0x10, t->Process->Pid);    // ClientId.UniqueProcess
            WriteUserU64(pml4, a2 + 0x18, t->Tid);             // ClientId.UniqueThread
            return STATUS_SUCCESS;
        }
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    case NT_QUERY_SYSTEM_TIME: {
        // NtQuerySystemTime. a1 = user LARGE_INTEGER*. 100 ns units since boot.
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process || !a1) return STATUS_UNSUCCESSFUL;
        WriteUserU64(t->Process->Cr3, a1, (u64)HAL::PitTicks() * 100000ULL);
        return STATUS_SUCCESS;
    }

    case NT_QUERY_PERF_COUNTER: {
        // NtQueryPerformanceCounter. a1 = counter*, a2 = optional frequency*.
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process || !a1) return STATUS_UNSUCCESSFUL;
        WriteUserU64(t->Process->Cr3, a1, (u64)HAL::PitTicks());
        if (a2) WriteUserU64(t->Process->Cr3, a2, 100ULL);   // PIT = 100 Hz
        return STATUS_SUCCESS;
    }

    case NT_PROTECT_VM: {
        // NtProtectVirtualMemory. a1=base, a2=size, a3=Win32 PAGE_* protection.
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return STATUS_UNSUCCESSFUL;
        u64 base = a1 & ~0xFFFULL;
        usize span = (usize)((a2 + (a1 & 0xFFF) + PAGE_SIZE - 1) / PAGE_SIZE);
        if (!span) span = 1;
        u64 flags = VMM::PTE_PRESENT | VMM::PTE_USER;
        if (a3 & 0xCC) flags |= VMM::PTE_WRITABLE;   // RW/WRITECOPY/EXEC_RW/EXEC_WRITECOPY
        for (usize i = 0; i < span; ++i)
            VMM::ProtectPageInto(t->Process->Cr3, base + i * PAGE_SIZE, flags);
        return STATUS_SUCCESS;
    }

    case NT_QUERY_VM: {
        // NtQueryVirtualMemory -> MEMORY_BASIC_INFORMATION (x64). a1=addr, a2=buf.
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process || !a2) return STATUS_UNSUCCESSFUL;
        u64 pml4 = t->Process->Cr3;
        u64 va  = a1 & ~0xFFFULL;
        u64 pte = VMM::GetPteInto(pml4, va);
        for (u32 i = 0; i < 48; ++i) WriteUserByte(pml4, a2 + i, 0);
        WriteUserU64(pml4, a2 + 0x00, va);             // BaseAddress
        WriteUserU64(pml4, a2 + 0x18, PAGE_SIZE);      // RegionSize
        if (pte & VMM::PTE_PRESENT) {
            WriteUserU64(pml4, a2 + 0x08, va);         // AllocationBase
            WriteUserU32(pml4, a2 + 0x20, 0x1000);     // State = MEM_COMMIT
            WriteUserU32(pml4, a2 + 0x24, (pte & VMM::PTE_WRITABLE) ? 0x04u : 0x02u); // PAGE_READWRITE/READONLY
            WriteUserU32(pml4, a2 + 0x28, 0x20000);    // Type = MEM_PRIVATE
        } else {
            WriteUserU32(pml4, a2 + 0x20, 0x10000);    // State = MEM_FREE
        }
        return STATUS_SUCCESS;
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
        if (h >= 0) return (u64)(h + 1);   // VFS handle: 1..3F
        // M21: not in VFS -> find or create in writable table
        u64 wi = WFindOrCreate(kpath);
        return wi < 16 ? (WFILE_HANDLE_BASE + wi) : 0;
    }

    case NT_READ_FILE: {
        // a1=handle, a2=user_buf_va, a3=offset, a4=len
        usize len = (usize)(a4 > 512 ? 512 : a4);
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return 0;
        u64 pml4 = t->Process->Cr3;

        if (a1 >= PIPE_HANDLE_BASE && a1 < PIPE_HANDLE_BASE+4) {
            KPipe& kp = g_pipes[a1 - PIPE_HANDLE_BASE];
            if (!kp.used) return 0;
            KThread* tp = Sched::CurrentThread();
            if (!tp || !tp->Process) return 0;
            u64 pml4p = tp->Process->Cr3;
            usize wlen = (usize)(a3 > 512 ? 512 : a3);
            usize written = 0;
            while (written < wlen && kp.size < 512) {
                kp.data[kp.size++] = ReadUserByte(pml4p, a2 + written);
                ++written;
            }
            return (u64)written;
        }
        if (a1 >= PIPE_HANDLE_BASE && a1 < PIPE_HANDLE_BASE+4) {
            // M22: read from named pipe
            KPipe& kp = g_pipes[a1 - PIPE_HANDLE_BASE];
            if (!kp.used || a3 >= kp.size) return 0;
            usize avail = kp.size - (usize)a3;
            if (len > avail) len = avail;
            KThread* tp = Sched::CurrentThread();
            if (!tp || !tp->Process) return 0;
            u64 pml4p = tp->Process->Cr3;
            for (usize i=0; i<len; ++i)
                WriteUserByte(pml4p, a2+i, kp.data[(usize)a3+i]);
            return (u64)len;
        }
        if (a1 >= WFILE_HANDLE_BASE && a1 < WFILE_HANDLE_MAX) {
            u32 widx = (u32)(a1 - WFILE_HANDLE_BASE);
            u8 rbuf[2048];
            u32 got2 = VFS::WRead(widx, a3, rbuf, (u32)len);
            for (u32 i = 0; i < got2; ++i) WriteUserByte(pml4, a2+i, rbuf[i]);
            return (u64)got2;
        }

        i32 h = (i32)((i64)a1 - 1);
        u64 offset = a3;
        u8 kbuf[512] = {};
        i64 got = VFS::Read(h, offset, kbuf, len);
        if (got <= 0) return 0;
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
        // Writes null-separated filenames: "name\0" repeated, then "\0"
        usize buf_len = (usize)(a2 > 2048 ? 2048 : a2);
        KThread* t = Sched::CurrentThread();
        if (!t || !t->Process) return 0;
        u64 pml4 = t->Process->Cr3;
        u64 pos  = a1;
        u64 end  = a1 + buf_len - 1;

        struct CbArgs { u64 pml4; u64 pos; u64 end; };
        static CbArgs cb_args;
        cb_args = { pml4, pos, end };
        VFS::ForEach([](const char* name, u64 /*size*/) {
            for (usize i = 0; name[i] && cb_args.pos < cb_args.end; ++i)
                WriteUserByte(cb_args.pml4, cb_args.pos++, (u8)name[i]);
            if (cb_args.pos < cb_args.end)
                WriteUserByte(cb_args.pml4, cb_args.pos++, 0);
        });
        // M30: list writable nodes (files and directories)
        VFS::ForEachWritable([](const char* name, u32 /*size*/, bool is_dir) {
            if (cb_args.pos >= cb_args.end - 4) return;
            // Prefix dirs with '[' and suffix with ']' so dir command shows type
            if (is_dir) {
                WriteUserByte(cb_args.pml4, cb_args.pos++, '[');
                for (usize j = 0; name[j] && cb_args.pos < cb_args.end; ++j)
                    WriteUserByte(cb_args.pml4, cb_args.pos++, (u8)name[j]);
                WriteUserByte(cb_args.pml4, cb_args.pos++, ']');
            } else {
                for (usize j = 0; name[j] && cb_args.pos < cb_args.end; ++j)
                    WriteUserByte(cb_args.pml4, cb_args.pos++, (u8)name[j]);
            }
            WriteUserByte(cb_args.pml4, cb_args.pos++, 0);
        });
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

    case NT_CREATE_FILE_SECTION: {
        // NtCreateSection over a VFS file. a1=name ptr, a2=name len.
        // Returns a section handle to map with NtMapViewOfSection.
        char name[64] = {};
        usize nlen = a2 < 63 ? (usize)a2 : 63;
        usize got = ReadUserBytes(a1, reinterpret_cast<u8*>(name), nlen);
        name[got] = 0;
        usize fsz = 0;
        const u8* fdata = VFS::FindFile(name, &fsz);
        if (!fdata || !fsz) return 0;
        u64 h = CreateSectionFromData(fdata, fsz);
        KDBG_TRACE("SYSCALL: NtCreateFileSection('%s', %llu bytes) -> handle %llu",
                   name, (u64)fsz, h);
        return h;
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

    case NT_CREATE_PROCESS: {
        // a1=name_va, a2=name_len -> loads PE from VFS, returns process handle
        KThread* ct = Sched::CurrentThread();
        if (!ct || !ct->Process) return 0;
        char fname[64] = {};
        usize flen = (usize)(a2 > 63 ? 63 : a2);
        for (usize i=0;i<flen;++i) fname[i]=(char)ReadUserByte(ct->Process->Cr3, a1+i);

        usize fsize=0;
        const u8* fdata = VFS::FindFile(fname, &fsize);
        if (!fdata || !fsize) { KDBG_ERROR("SYSCALL: NtCreateProcess: '%s' not found",fname); return 0; }

        u64 ccr3 = VMM::CreateUserPml4();
        if (!ccr3) return 0;
        KProcess* cp = PS::CreateProcess(fname, ccr3);
        if (!cp) return 0;

        // Load ntdll into child (harmless if child has no imports)
        u64 _dummy=0;
        LDR::LoadAndRegister("ntdll.dll", s_ntdll_pe, s_ntdll_pe_size, ccr3, s_ntdll_image_base, &_dummy);

        // Read child PE's preferred ImageBase from its own Optional Header
        u64 image_base = 0;
        if (fsize >= 64) {
            u32 lfanew = *reinterpret_cast<const u32*>(fdata + 60);
            // PE32+: signature(4) + COFF(20) + magic(2) at lfanew; ImageBase at +24 from opt hdr
            if (lfanew + 4 + 20 + 32 <= fsize &&
                fdata[lfanew]=='P' && fdata[lfanew+1]=='E') {
                image_base = *reinterpret_cast<const u64*>(fdata + lfanew + 4 + 20 + 24);
            }
        }
        if (!image_base) image_base = 0x500000000ULL; // fallback

        // Load child PE
        u64 entry_va=0;
        NTSTATUS st = LDR::LoadPe(fdata, fsize, ccr3, image_base, &entry_va);
        if (!NT_SUCCESS(st)) { KDBG_ERROR("SYSCALL: NtCreateProcess: LoadPe failed"); return 0; }

        // Allocate child stack well ABOVE image_base to avoid overlapping PE sections.
        // (UserHeapCursor starts at 0x500000000; PE sections also map there starting
        //  at image_base + RVA.  Using image_base + 0x100000 gives 1MB of headroom.)
        u64 stack_base = image_base + 0x100000ULL;
        constexpr usize CSTK=4;
        u64 fl=VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER;
        for (usize i=0;i<CSTK;++i) {
            u64 pp=PMM::AllocPage(); if(!pp)return 0;
            for(usize j=0;j<PAGE_SIZE;++j)((u8*)pp)[j]=0;
            if(!VMM::MapPageInto(ccr3,stack_base+i*PAGE_SIZE,pp,fl))return 0;
        }
        KThread* ct2=PS::CreateUserThread(cp,fname,entry_va,stack_base+CSTK*PAGE_SIZE);
        if(!ct2)return 0;
        Sched::AddThread(ct2);

        for (u64 i=0;i<PROC_TABLE_SIZE;++i) if(!s_proc_table[i]) {
            s_proc_table[i]=cp;
            KDBG_INFO("SYSCALL: NtCreateProcess('%s') -> handle 0x%llx",fname,PROC_HANDLE_BASE+i);
            return PROC_HANDLE_BASE+i;
        }
        return 0;
    }

    case NT_WAIT_MULTI: {
        // a1=count, a2=handles_va, a3=wait_all, a4=timeout_ms
        u32 cnt=(u32)a1; if(!cnt||cnt>8)return (u64)STATUS_INVALID_PARAMETER;
        KThread* wt=Sched::CurrentThread(); if(!wt||!wt->Process)return (u64)STATUS_INVALID_PARAMETER;
        u64 pml4=wt->Process->Cr3;
        u64 hs[8]={};
        for(u32 i=0;i<cnt;++i){
            u64 h=0;
            for(u32 b=0;b<8;++b) h|=(u64)ReadUserByte(pml4,a2+i*8+b)<<(b*8);
            hs[i]=h;
        }
        bool wall=(a3!=0);
        // poll loop (simplified wait-any / wait-all)
        u64 deadline=(a4==(u64)-1)?(u64)-1:(u64)HAL::PitTicks()+(u64)a4*100/1000+1;
        bool acquired[8]={};
        for(;;){
            u32 done=0;
            for(u32 i=0;i<cnt;++i){
                if(acquired[i]){++done;continue;}
                // non-blocking check for process handles (simplest)
                if(hs[i]>=PROC_HANDLE_BASE){
                    u64 idx2=hs[i]-PROC_HANDLE_BASE;
                    if(idx2<PROC_TABLE_SIZE&&s_proc_table[idx2]&&s_proc_table[idx2]->exited){
                        acquired[i]=true;++done;
                    }
                } else {
                    // For events/sema/mutant: check via NtWaitForSingleObject with timeout=0
                    // For now mark as done if handle==0 (ignore invalid)
                    if(!hs[i]){acquired[i]=true;++done;}
                }
            }
            if(!wall && done>0){ for(u32 i=0;i<cnt;++i)if(acquired[i])return i; }
            if( wall && done==cnt) return (u64)STATUS_SUCCESS;
            if(a4==0)return (u64)STATUS_TIMEOUT;
            if(deadline!=(u64)-1&&(u64)HAL::PitTicks()>=deadline)return (u64)STATUS_TIMEOUT;
            Sched::Sleep(10);
        }
    }

    case NT_CREATE_NAMED_PIPE: {
        // a1=name_va, a2=name_len -> handle (PIPE_HANDLE_BASE+idx) or 0
        usize plen = (usize)(a2 > 31 ? 31 : a2);
        char kname[32] = {};
        ReadUserBytes(a1, reinterpret_cast<u8*>(kname), plen);
        kname[plen] = '\0';
        // find existing
        for (u32 i=0;i<4;++i)
            if (g_pipes[i].used) {
                bool ok=true;
                for(int j=0;j<31;++j){if(g_pipes[i].name[j]!=kname[j]){ok=false;break;}if(!kname[j])break;}
                if (ok) { KDBG_INFO("SYSCALL: NtCreateNamedPipe find existing [%u] '%s' -> 0x%llx", i, kname, PIPE_HANDLE_BASE+i); return PIPE_HANDLE_BASE + i; }
            }
        // create new
        for (u32 i=0;i<4;++i)
            if (!g_pipes[i].used) {
                g_pipes[i].used=true; g_pipes[i].size=0;
                for(int j=0;j<31&&kname[j];++j) g_pipes[i].name[j]=kname[j];
                KDBG_TRACE("SYSCALL: NtCreateNamedPipe('%s') -> 0x%llx", kname, PIPE_HANDLE_BASE+i);
                return PIPE_HANDLE_BASE + i;
            }
        return 0; // table full
    }

    case NT_VGA_CLEAR: {
        VGA::ClearScreen();   // fast clear: blanks rows 1+, keeps header bar
        return (u64)STATUS_SUCCESS;
    }

    case NT_OPEN_EVENT: {
        // a1=name_va, a2=name_len -> find existing named event, return handle
        KThread* ot=Sched::CurrentThread(); if(!ot||!ot->Process)return 0;
        char oname[48]={};
        usize olen=(usize)(a2>47?47:a2);
        for(usize i=0;i<olen;++i) oname[i]=(char)ReadUserByte(ot->Process->Cr3,a1+i);
        for(usize i=0;i<EVENT_TABLE_SIZE;++i)
            if(s_events[i]&&s_events[i]->name[0]){
                const char* en=s_events[i]->name; bool match=true;
                for(usize j=0;j<olen;++j)if(en[j]!=oname[j]){match=false;break;}
                if(match&&!en[olen]) return (u64)(i+1);
            }
        return 0;
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

void SetupTestPipe(const char* name, const u8* data, u32 len) {
    for (u32 i=0;i<4;++i)
        if (!g_pipes[i].used) {
            g_pipes[i].used=true; g_pipes[i].size=len>512?512:len;
            for(int j=0;j<31&&name[j];++j) g_pipes[i].name[j]=name[j];
            for(u32 j=0;j<g_pipes[i].size;++j) g_pipes[i].data[j]=data[j];
            return;
        }
}

void SetCommands(const char** cmds, u32 count) {
    s_cmd_idx = 0;
    s_cmd_count = 0;
    for (u32 i = 0; i < count && i < 8; ++i)
        s_cmds[s_cmd_count++] = cmds[i];
    KDBG_INFO("SYSCALL: command queue loaded (%u commands)", s_cmd_count);
}

// Verify file-backed section objects: create a section from a VFS file, then
// map it into a fresh user address space and read the bytes back -- the core
// of memory-mapped files / DLL image mapping.
bool SelfTestFileSection() {
    usize sz = 0;
    const u8* data = VFS::FindFile("hello3.exe", &sz);
    if (!data || sz < 2) {
        KDBG_INFO("SECTEST: hello3.exe not in VFS - skipping");
        return true;
    }
    u64 h = CreateSectionFromData(data, sz);
    if (!h) { KDBG_ERROR("SECTEST: section create failed"); return false; }
    KSection& sec = s_sections[h - 1];

    bool copy_ok = (reinterpret_cast<u8*>(sec.phys[0])[0] == 'M' &&
                    reinterpret_cast<u8*>(sec.phys[0])[1] == 'Z');

    bool map_ok = false;
    u64 cr3 = VMM::CreateUserPml4();
    if (cr3) {
        u64 va = 0x600000000ULL;
        bool mapped = true;
        for (usize j = 0; j < sec.page_count; ++j)
            if (!VMM::MapPageInto(cr3, va + j * PAGE_SIZE, sec.phys[j],
                                  VMM::PTE_PRESENT | VMM::PTE_USER)) { mapped = false; break; }
        if (mapped) {
            u64 phys = VMM::TranslateInPml4(cr3, va);
            if (phys) {
                u8* mp = reinterpret_cast<u8*>(phys);
                map_ok = (mp[0] == 'M' && mp[1] == 'Z');
            }
        }
    }

    KDBG_INFO("SECTEST: file section h=%llu pages=%llu copy=%s map=%s (MZ readback)",
              h, (u64)sec.page_count, copy_ok ? "ok" : "BAD", map_ok ? "ok" : "BAD");

    // Release the test section's pages.
    for (usize j = 0; j < sec.page_count; ++j) PMM::FreePage(sec.phys[j]);
    sec.in_use = false;
    return copy_ok && map_ok;
}

} // namespace SYSCALL
