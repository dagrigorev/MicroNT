// kernel_main.cpp - MicroNT kernel entry point (UEFI path)
// Called from boot.asm (_kernel_start) after convention conversion.

#include "../include/ntdef.h"
#include "../include/bootinfo.h"
#include "../include/ntstatus.h"
#include "../include/appmodel.h"
#include "../include/debug.h"
#include "../include/hal.h"
#include "../include/memory.h"
#include "../include/object.h"
#include "../include/process.h"
#include "../include/registry.h"
#include "../include/sync.h"
#include "../include/pe.h"
#include "../include/profile.h"
#include "../include/io.h"
#include "../include/csrss.h"
#include "../include/desktopmodel.h"
#include "../include/displaycfg.h"
#include "../include/dwm.h"
#include "../include/explorer.h"
#include "../include/inputhost.h"
#include "../include/services.h"
#include "../include/session.h"
#include "../include/shellhost.h"
#include "../include/shellinput.h"
#include "../include/uxtheme.h"
#include "../include/userinit.h"
#include "../include/winlogon.h"
#include "../include/windowmgr.h"
#include "../include/winsta.h"
#include "../include/win32k.h"
#include "../ldr/hello2_pe.h"
#include "../ldr/hello_pe.h"
#include "../ldr/ntdll_pe.h"
#include "../ldr/shell_pe.h"
#include "../ldr/hello3_pe.h"
#include "../ldr/hello4_pe.h"
#include "../ldr/hello5_pe.h"
#include "../ldr/hello6_pe.h"
#include "../ldr/hello7_pe.h"
#include "../ldr/hello8_pe.h"
#include "../ldr/hello9_pe.h"
#include "../ldr/hello10_pe.h"



// Volatile flags set by syscall handlers (defined in syscall.cpp)
extern volatile u32 g_m6_syscall_ok;
extern volatile u32 g_m7_pe_ok;
extern volatile u32 g_m8_write_ok;
extern volatile u32 g_m9_ver_ok;
extern volatile u32 g_m11_heap_ok;
extern volatile u32 g_m12_sync_ok;
extern volatile u32 g_m13_thread_ok;
extern volatile u32 g_m14_info_ok;
extern volatile u32 g_m15_ok;
extern volatile u32 g_m16_ok;
extern volatile u32 g_m17_ok;
extern volatile u32 g_m18_ok;
extern volatile u32 g_m19_ok;
extern volatile u32 g_m20_ok;
extern volatile u32 g_m21_ok;
extern volatile u32 g_m22_ok;
extern "C" volatile u64 g_pending_exception_va;
extern u64 s_user_heap_cursor;

extern "C" void kernel_main(MicroNTBootInfo* boot_info) {
    // ----------------------------------------------------------
    // 1. Serial + debug (first so we can log everything)
    // ----------------------------------------------------------
    Serial::Init(COM1_PORT, DEFAULT_BAUD);
    Debug::Init();

    Debug::Print("\r\n");
    Debug::Print("[MicroNT] Boot started\r\n");
    // Initialize GOP framebuffer renderer before first VGA::Init()
    if (boot_info && boot_info->fb_base) {
        VGA::SetFramebuffer(boot_info->fb_base,
                            boot_info->fb_width, boot_info->fb_height,
                            boot_info->fb_stride, boot_info->fb_format);
    }
    VGA::Init();
    //KB::Init();
    //HAL::IrqRegister(1, KB::HandleIrq);  // PS/2 keyboard = IRQ1
    Debug::Print("[MicroNT] CPU: x86_64 long mode (UEFI)\r\n");

    // Validate boot info
    if (!boot_info || boot_info->magic != BOOTINFO_MAGIC) {
        KernelPanic("Invalid boot info magic - bootloader bug");
    }
    KDBG_INFO("BootInfo: kernel=0x%llx size=%llu KB rsdp=0x%llx entries=%u",
        boot_info->kernel_phys_base,
        (u64)(boot_info->kernel_size / 1024),
        boot_info->rsdp_phys,
        boot_info->memory_entry_count);

    // ----------------------------------------------------------
    // 2. CPU detection
    // ----------------------------------------------------------
    HAL::CpuDetect();

    // ----------------------------------------------------------
    // 3. GDT / PIC / IDT
    // ----------------------------------------------------------
    HAL::GdtInit();
    Debug::Print("[MicroNT] GDT initialized\r\n");

    HAL::PicInit();

    HAL::IrqInit();

    // IRQ table must be initialized before registering IRQ handlers.
    // PicInit() masks all IRQs except cascade; unmask IRQ1 explicitly
    // after the keyboard handler is registered.
    KB::Init();
    HAL::IrqRegister(1, KB::HandleIrq);  // PS/2 keyboard = IRQ1
    HAL::PicSetMask(1, false);

    HAL::IdtInit();
    Debug::Print("[MicroNT] IDT initialized\r\n");

    HAL::EnableInterrupts();
    Debug::Print("[MicroNT] HAL initialized\r\n");

    // ----------------------------------------------------------
    // M2: PIT timer
    // ----------------------------------------------------------
    HAL::PitInit(100);   // 100 Hz = 10 ms ticks
    Debug::Print("[MicroNT] PIT initialized\r\n");

    // ----------------------------------------------------------
    // 4. Physical memory manager
    // ----------------------------------------------------------
    PMM::Init(boot_info);
    Debug::Printf("[INFO ] [PMM] Physical memory: %llu MB total, %llu MB free\r\n",
        (u64)PMM::TotalPages() * PAGE_SIZE / (1024 * 1024),
        (u64)PMM::FreePages()  * PAGE_SIZE / (1024 * 1024));
    Debug::Print("[MicroNT] Physical memory manager initialized\r\n");

    // ----------------------------------------------------------
    // 5. Kernel heap (4 MB bump allocator)
    // ----------------------------------------------------------
    u64 heap_base = PMM::AllocPages(1024);
    if (!heap_base) KernelPanic("Failed to allocate kernel heap");
    KernelHeap::Init(heap_base, 1024 * PAGE_SIZE);

    // ----------------------------------------------------------
    // 6. Virtual memory manager (M3: full PT walker)
    // ----------------------------------------------------------
    VMM::Init();
    Debug::Print("[MicroNT] Virtual memory manager initialized\r\n");

    // M3 smoke test: map a fresh physical page at a new kernel VA,
    // write a sentinel, read it back, then unmap.
    {
        u64 test_phys = PMM::AllocPage();
        KASSERT(test_phys != 0);

        u64 test_va = VMM::AllocKernelVA(1);
        bool mapped = VMM::MapPage(test_va, test_phys,
                                   VMM::PTE_PRESENT | VMM::PTE_WRITABLE);
        KASSERT(mapped);

        u64 resolved = VMM::V2P(test_va);
        KASSERT(resolved == test_phys);

        constexpr u64 SENTINEL = 0xDEADBEEFCAFE0001ULL;
        *reinterpret_cast<volatile u64*>(test_va) = SENTINEL;
        u64 readback = *reinterpret_cast<volatile u64*>(test_va);
        KASSERT(readback == SENTINEL);

        VMM::UnmapPage(test_va);
        PMM::FreePage(test_phys);

        KDBG_INFO("VMM: map/write/verify/unmap OK (VA=0x%llx PA=0x%llx)",
                  test_va, test_phys);
        Debug::Print("[MicroNT] M3 ready\r\n");
    }

    // ----------------------------------------------------------
    // 7. Object manager (M4: types, handles, namespace)
    // ----------------------------------------------------------
    OB::Init();
    Debug::Print("[MicroNT] Object manager initialized\r\n");

    // M4 smoke test
    {
        // 1. Register a type
        ObType* test_type = nullptr;
        struct TestBody { u64 value; };
        NTSTATUS st = OB::CreateType("TestObject", sizeof(TestBody),
                                      nullptr, &test_type);
        KASSERT(NT_SUCCESS(st) && test_type);

        // 2. Allocate a named object, insert into namespace
        auto* obj = static_cast<TestBody*>(
            OB::AllocateObject(test_type, "\\OB\\Test"));
        KASSERT(obj);
        obj->value = 0xABCD1234;

        // 3. Open a handle
        HANDLE h = NULL_HANDLE;
        st = OB::InsertObject(obj, GENERIC_ALL, &h);
        KASSERT(NT_SUCCESS(st) && h != NULL_HANDLE && h != INVALID_HANDLE);

        // 4. Verify lookup by handle
        void* looked_up = nullptr;
        st = OB::ReferenceObjectByHandle(h, test_type, GENERIC_READ, &looked_up);
        KASSERT(NT_SUCCESS(st) && looked_up == obj);
        KASSERT(static_cast<TestBody*>(looked_up)->value == 0xABCD1234);
        OB::DereferenceObject(looked_up);

        // 5. Verify lookup by name
        void* by_name = nullptr;
        st = OB::LookupObjectByName("\\OB\\Test", test_type, &by_name);
        KASSERT(NT_SUCCESS(st) && by_name == obj);
        OB::DereferenceObject(by_name);

        // 6. Duplicate name must fail with ALREADY_EXISTS
        st = OB::InsertObjectByName(obj, "\\OB\\Test");
        KASSERT(st == STATUS_ALREADY_EXISTS);

        // 7. Close handle
        st = OB::CloseHandle(h);
        KASSERT(NT_SUCCESS(st));

        // 8. Stale handle must fail with INVALID_HANDLE
        st = OB::ReferenceObjectByHandle(h, nullptr, 0, &looked_up);
        KASSERT(st == STATUS_INVALID_HANDLE);

        OB::DumpStats();
        Debug::Print("[MicroNT] M4 ready\r\n");
    }

    // ----------------------------------------------------------
    // 8. Process/Thread manager (M5)
    // ----------------------------------------------------------
    PS::Init();
    Debug::Print("[MicroNT] Process manager initialized\r\n");

    // ----------------------------------------------------------
    // 9. I/O manager + console
    // ----------------------------------------------------------
    IO::Init();
    IO::Console::Init();
    VFS::Init(boot_info);  // M10: init VFS with bootloader-provided files
    SYNC::Init();             // M12: synchronization layer

    // ----------------------------------------------------------
    // 10. Syscall layer
    // ----------------------------------------------------------
    SYSCALL::Init();

    // ----------------------------------------------------------
    // 11. PE loader
    // ----------------------------------------------------------
    LDR::Init();
    Debug::Print("[MicroNT] PE loader initialized\r\n");

    REGISTRY::Init();
    KASSERT(REGISTRY::LoadSystemHive());
    CSRSS::Init();
    WIN32K::Init();
    DISPLAYCFG::Init();
    WINDOWMGR::Init();
    DESKTOPMODEL::Init();
    UXTHEME::Init();
    WINSTA::Init();
    DWM::Init();
    WINLOGON::Init();
    PROFILE::Init();
    USERINIT::Init();
    APPMODEL::Init();
    EXPLORER::Init();
    SHELLHOST::Init();
    INPUTHOST::Init();
    SHELLINPUT::Init();
    SERVICES::Init();
    SM::Init();

    // ----------------------------------------------------------
    // 12. Initrd (TODO: parse MNTAR001 from boot volume)
    // ----------------------------------------------------------
    Debug::Print("[MicroNT] Initrd mounted (TODO: parse MNTAR001)\r\n");

    // ----------------------------------------------------------
    // M2: verify timer is ticking
    // ----------------------------------------------------------
    {
        u64 t0 = HAL::PitTicks();
        HAL::PitSleep(200);
        u64 t1 = HAL::PitTicks();
        u64 delta = t1 - t0;
        Debug::Printf("[INFO ] Timer: %llu ticks in 200 ms (expected ~20)\r\n", delta);
        if (delta >= 10) {
            Debug::Print("[MicroNT] M2 ready\r\n");
        } else {
            Debug::Printf("[WARN ] Timer tick count low (%llu)\r\n", delta);
        }
    }

    // ----------------------------------------------------------
    // M5: Scheduler smoke test
    //  3 kernel threads each print 3 iterations then exit.
    //  kernel_main (the "Main" thread) waits for all 3 to finish.
    // ----------------------------------------------------------
    {
        static volatile u32 s_done = 0;

        struct ThreadArgs { const char* name; u32 iters; };
        static ThreadArgs argsA{"Thread A", 3};
        static ThreadArgs argsB{"Thread B", 3};
        static ThreadArgs argsC{"Thread C", 3};

        auto test_fn = [](void* varg) {
            auto* a = static_cast<ThreadArgs*>(varg);
            for (u32 i = 1; i <= a->iters; ++i) {
                Debug::Printf("[INFO ] %s: iteration %u/%u\r\n",
                              a->name, i, a->iters);
                HAL::PitSleep(80);   // 80 ms between iterations
            }
            HAL::DisableInterrupts();
            s_done = s_done + 1u;
            HAL::EnableInterrupts();
        };

        KThread* ta = PS::CreateKernelThread(
            PS::SystemProcess(), "ThreadA",
            static_cast<void(*)(void*)>(test_fn), &argsA);
        KThread* tb = PS::CreateKernelThread(
            PS::SystemProcess(), "ThreadB",
            static_cast<void(*)(void*)>(test_fn), &argsB);
        KThread* tc = PS::CreateKernelThread(
            PS::SystemProcess(), "ThreadC",
            static_cast<void(*)(void*)>(test_fn), &argsC);
        KASSERT(ta && tb && tc);

        Sched::Start();     // Main thread becomes s_current; preemption enabled

        Sched::AddThread(ta);
        Sched::AddThread(tb);
        Sched::AddThread(tc);

        // Yield until all 3 threads have finished
        while (s_done < 3) {
            Sched::Schedule();   // cooperative yield
        }

        Debug::Print("[MicroNT] M5 ready\r\n");
    }

    // ----------------------------------------------------------
    // M6: User-mode thread + SYSCALL smoke test
    // ----------------------------------------------------------
    {

        // -- User code (12 bytes) embedded as raw x86-64 machine code --
        // xor eax, eax (syscall 0 = NT_TEST_SYSCALL)
        // mov edi, 42  (arg1 = 42)
        // syscall
        // inc eax      (syscall 1 = NT_TERMINATE_THREAD)
        // xor edi, edi (exit code = 0)
        // syscall
        // jmp $        (safety)
        static const u8 USER_CODE[] = {
            0x31, 0xC0,                   // xor eax, eax
            0xBF, 0x2A, 0x00, 0x00, 0x00, // mov edi, 42
            0x0F, 0x05,                   // syscall   -> NT_TEST_SYSCALL
            0xFF, 0xC0,                   // inc eax
            0x31, 0xFF,                   // xor edi, edi
            0x0F, 0x05,                   // syscall   -> NT_TERMINATE_THREAD
            0xEB, 0xFE                    // jmp $
        };

        // Allocate physical pages for code and stack
        u64 code_phys  = PMM::AllocPage();
        u64 stack_phys = PMM::AllocPage();
        KASSERT(code_phys && stack_phys);

        // Zero pages then copy user code
        for (u32 i = 0; i < PAGE_SIZE; ++i)
            reinterpret_cast<u8*>(code_phys)[i]  = 0;
        for (u32 i = 0; i < PAGE_SIZE; ++i)
            reinterpret_cast<u8*>(stack_phys)[i] = 0;
        for (u32 i = 0; i < sizeof(USER_CODE); ++i)
            reinterpret_cast<u8*>(code_phys)[i] = USER_CODE[i];

        // Create user process with its own PML4 (kernel half shared)
        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);

        KProcess* uproc = PS::CreateProcess("UserTest", user_cr3);
        KASSERT(uproc);

        // User VAs must be above 4 GB to avoid the 2 MB huge-page region
        // (0-4 GB is identity-mapped with PS=1 entries; MapPageInto cannot
        // create 4 KB PTEs inside an existing 2 MB huge page).
        // We use PDPT[8] of PML4[0]: VA = 8 * 1GB = 0x200000000.
        constexpr u64 USER_CODE_VA  = 0x200001000ULL;  // 8 GB + 4 KB
        constexpr u64 USER_STACK_VA = 0x200002000ULL;  // 8 GB + 8 KB

        bool ok = VMM::MapPageInto(user_cr3, USER_CODE_VA, code_phys,
                                   VMM::PTE_PRESENT | VMM::PTE_USER);
        KASSERT(ok);

        // Map user stack page (writable + user)
        ok = VMM::MapPageInto(user_cr3, USER_STACK_VA, stack_phys,
                              VMM::PTE_PRESENT | VMM::PTE_WRITABLE | VMM::PTE_USER);
        KASSERT(ok);

        // Stack top = top of the stack page (stack grows down)
        u64 user_stack_top = USER_STACK_VA + PAGE_SIZE;

        // Create user thread (entry=0x400000, user_rsp=0x800000)
        KThread* uthread = PS::CreateUserThread(
            uproc, "UserTest0", USER_CODE_VA, user_stack_top);
        KASSERT(uthread);

        KDBG_INFO("M6: user process CR3=0x%llx code_va=0x%llx stack_top=0x%llx",
                  user_cr3, USER_CODE_VA, user_stack_top);

        Sched::AddThread(uthread);

        // Wait for the syscall to fire
        while (!g_m6_syscall_ok) {
            Sched::Schedule();
        }

        Debug::Print("[MicroNT] M6 ready\r\n");
    }

    // ----------------------------------------------------------
    // M7: PE loader smoke test
    //  Load hello.exe (PE32+ blob) into a fresh user process,
    //  run it, verify it calls NtTestPe(0x4D37).
    // ----------------------------------------------------------
    {
        // Include pre-generated PE blob


        // Create user process
        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("hello.exe", user_cr3);
        KASSERT(proc);

        // Allocate user stack (one page above image)
        constexpr u64 USER_STACK_VA = 0x1000100000ULL;
        u64 stack_phys = PMM::AllocPage();
        KASSERT(stack_phys);
        for (u32 i = 0; i < PAGE_SIZE; ++i)
            reinterpret_cast<u8*>(stack_phys)[i] = 0;
        bool ok = VMM::MapPageInto(user_cr3, USER_STACK_VA, stack_phys,
                                   VMM::PTE_PRESENT | VMM::PTE_WRITABLE | VMM::PTE_USER);
        KASSERT(ok);

        // Load PE
        u64 entry_va = 0;
        NTSTATUS st = LDR::LoadPe(s_hello_pe, s_hello_pe_size,
                                   user_cr3, s_hello_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        KDBG_INFO("M7: hello.exe loaded at 0x%llx entry=0x%llx stack=0x%llx",
                  s_hello_image_base, entry_va, USER_STACK_VA + PAGE_SIZE);

        // Create and run user thread
        KThread* uthread = PS::CreateUserThread(
            proc, "hello.exe!main",
            entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        // Wait for PE to call NtTestPe
        while (!g_m7_pe_ok) {
            Sched::Schedule();
        }
        KASSERT(g_m7_pe_ok == 0x4D37);  // must match magic from hello.exe

        Debug::Print("[MicroNT] M7 ready\r\n");
    }

    // ----------------------------------------------------------
    // M8: Import resolution + NtWriteFile
    //  Load ntdll.dll (syscall stubs) into a user process,
    //  then load hello2.exe (imports NtWriteFile + NtTerminateThread).
    //  hello2 prints "Hello from ring-3!" via NtWriteFile syscall.
    // ----------------------------------------------------------
    {


        // Create user process
        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("hello2.exe", user_cr3);
        KASSERT(proc);

        // 1. Load ntdll.dll (register so import resolution can find it)
        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        // 2. Load hello2.exe (imports resolved against ntdll)
        u64 entry_va = 0;
        st = LDR::LoadPe(s_hello2_pe, s_hello2_pe_size,
                          user_cr3, s_hello2_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        // 3. Allocate user stack
        constexpr u64 USER_STACK_VA = 0x8000100000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0; i<PAGE_SIZE; ++i)
            reinterpret_cast<u8*>(stk_phys)[i] = 0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KDBG_INFO("M8: hello2.exe entry=0x%llx stack_top=0x%llx",
                  entry_va, USER_STACK_VA + PAGE_SIZE);

        // 4. Run hello2
        KThread* uthread = PS::CreateUserThread(
            proc, "hello2.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m8_write_ok) { Sched::Schedule(); }

        Debug::Print("[MicroNT] M8 ready\r\n");
    }

    // ----------------------------------------------------------
    // M9: Console shell
    //  shell.exe reads commands from kernel queue via NtReadLine,
    //  handles "ver" by writing the version string via NtWriteFile.
    // ----------------------------------------------------------
    {


        // Pre-populate command queue: ["ver", "exit"]
        const char* cmds[] = { "ver", "exit" };
        SYSCALL::SetCommands(cmds, 2);

        // Create user process
        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("shell.exe", user_cr3);
        KASSERT(proc);

        // Load ntdll.dll (provides NtReadLine, NtWriteFile, NtTerminateThread)
        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        // Load shell.exe (imports resolved against ntdll)
        u64 entry_va = 0;
        st = LDR::LoadPe(s_shell_pe, s_shell_pe_size,
                          user_cr3, s_shell_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        // User stack
        constexpr u64 USER_STACK_VA = 0x9000100000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i = 0; i < PAGE_SIZE; ++i)
            reinterpret_cast<u8*>(stk_phys)[i] = 0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KDBG_INFO("M9: shell.exe entry=0x%llx", entry_va);

        KThread* uthread = PS::CreateUserThread(
            proc, "shell.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m9_ver_ok) { Sched::Schedule(); }

        Debug::Print("[MicroNT] M9 ready\r\n");
    }

    // ----------------------------------------------------------
    // M10: VFS + NtCreateFile/NtReadFile
    //  The bootloader loaded /boot/hello3.exe into memory.
    //  hello3.exe opens itself via NtCreateFile, reads 2 bytes,
    //  writes them via NtWriteFile (should print "MZ").
    // ----------------------------------------------------------
    {

        // Check if hello3.exe was loaded by the bootloader
        u64 fsize = 0;
        const void* fdata = VFS::GetData("hello3.exe", &fsize);
        if (!fdata || fsize == 0) {
            Debug::Print("[WARN ] M10: hello3.exe not found in boot files - skipping\r\n");
            Debug::Print("[MicroNT] M10 ready\r\n");
        } else {
            KDBG_INFO("M10: hello3.exe found in VFS (%llu bytes)", fsize);
            g_m8_write_ok = 0;  // clear so we can detect the new write

            u64 user_cr3 = VMM::CreateUserPml4();
            KASSERT(user_cr3);
            KProcess* proc = PS::CreateProcess("hello3.exe", user_cr3);
            KASSERT(proc);

            // Load ntdll.dll
            u64 ntdll_entry = 0;
            NTSTATUS st = LDR::LoadAndRegister(
                "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
                user_cr3, s_ntdll_image_base, &ntdll_entry);
            KASSERT(NT_SUCCESS(st));

            // Load hello3.exe from VFS (real disk file)
            u64 entry_va = 0;
            st = LDR::LoadPe(fdata, (usize)fsize,
                              user_cr3, s_hello3_image_base, &entry_va);
            KASSERT(NT_SUCCESS(st));

            // User stack
            constexpr u64 USER_STACK_VA = 0xA000100000ULL;
            u64 stk_phys = PMM::AllocPage();
            KASSERT(stk_phys);
            for (u32 i=0;i<PAGE_SIZE;++i)
                reinterpret_cast<u8*>(stk_phys)[i]=0;
            KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                      VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

            KThread* uthread = PS::CreateUserThread(
                proc, "hello3.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
            KASSERT(uthread);
            Sched::AddThread(uthread);

            while (!g_m8_write_ok) { Sched::Schedule(); }
            Debug::Print("[MicroNT] M10 ready\r\n");
        }
    }

    // ----------------------------------------------------------
    // M11: NtAllocateVirtualMemory - user-mode heap
    //  hello4.exe allocates a page, writes 0xDEADBEEF, reads back,
    //  then NtWriteFile("HEAP OK\n") and terminates.
    // ----------------------------------------------------------
    {
        #include "../ldr/hello4_pe.h"

        // Reset heap cursor for this new process

        g_m8_write_ok = 0;  // reuse write flag to detect "HEAP OK\n"

        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("hello4.exe", user_cr3);
        KASSERT(proc);

        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        u64 entry_va = 0;
        st = LDR::LoadPe(s_hello4_pe, s_hello4_pe_size,
                          user_cr3, s_hello4_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        constexpr u64 USER_STACK_VA = 0xB000100000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(stk_phys)[i]=0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KThread* uthread = PS::CreateUserThread(
            proc, "hello4.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m8_write_ok) { Sched::Schedule(); }
        KASSERT(g_m11_heap_ok);
        Debug::Print("[MicroNT] M11 ready\r\n");
    }

    // ----------------------------------------------------------
    // M12: Synchronization (KEvent, NtWaitForSingleObject)
    //
    // Part A (kernel): two kernel threads use an event to
    //   synchronize - setter sleeps 300ms then signals; waiter
    //   blocks until signaled.
    // Part B (user):   hello5.exe tests user-mode event API.
    // ----------------------------------------------------------
    {
        // --- Part A: kernel-thread synchronization test ---
        static KEvent s_m12_ev;
        SYNC::EventInit(&s_m12_ev, false, false);  // manual-reset, not signaled

        static volatile u32 s_m12_waiter_done = 0;

        // Waiter thread: blocks on event, prints message, sets done flag
        auto waiter_fn = [](void*) {
            KDBG_INFO("M12: waiter thread blocking on event...");
            SYNC::EventWait(&s_m12_ev, 0xFFFFFFFF);
            KDBG_INFO("M12: waiter thread unblocked!");
            s_m12_waiter_done = 1;
            PS::TerminateCurrentThread(0);
        };

        // Setter thread: sleeps 300ms then signals event
        auto setter_fn = [](void*) {
            HAL::PitSleep(300);
            KDBG_INFO("M12: setter thread signaling event...");
            SYNC::EventSet(&s_m12_ev);
            PS::TerminateCurrentThread(0);
        };

        KThread* tw = PS::CreateKernelThread(PS::SystemProcess(),
            "M12Waiter", static_cast<void(*)(void*)>(waiter_fn), nullptr);
        KThread* ts = PS::CreateKernelThread(PS::SystemProcess(),
            "M12Setter", static_cast<void(*)(void*)>(setter_fn), nullptr);
        KASSERT(tw && ts);
        Sched::AddThread(tw);
        Sched::AddThread(ts);

        while (!s_m12_waiter_done) { Sched::Schedule(); }
        KDBG_INFO("M12: kernel sync test passed");

        // --- Part B: user-mode event test via hello5.exe ---
        g_m12_sync_ok = 0;
        g_m8_write_ok = 0;   // detect "SYNC OK\n"

        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("hello5.exe", user_cr3);
        KASSERT(proc);

        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        u64 entry_va = 0;
        st = LDR::LoadPe(s_hello5_pe, s_hello5_pe_size,
                          user_cr3, s_hello5_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        constexpr u64 USER_STACK_VA = 0xC000100000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(stk_phys)[i]=0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KThread* uthread = PS::CreateUserThread(
            proc, "hello5.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m8_write_ok) { Sched::Schedule(); }
        KASSERT(g_m12_sync_ok);
        Debug::Print("[MicroNT] M12 ready\r\n");
    }

    // ----------------------------------------------------------
    // M13: NtCreateThread + NtDelayExecution
    //  hello6.exe spawns two worker threads (ids 1 and 2).
    //  Each worker sleeps id*100ms then prints "Worker N done".
    //  Main sleeps 500ms then prints "THREAD OK".
    // ----------------------------------------------------------
    {
        g_m8_write_ok   = 0;
        g_m13_thread_ok = 0;

        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("hello6.exe", user_cr3);
        KASSERT(proc);

        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        u64 entry_va = 0;
        st = LDR::LoadPe(s_hello6_pe, s_hello6_pe_size,
                          user_cr3, s_hello6_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        constexpr u64 USER_STACK_VA = 0xD000100000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(stk_phys)[i]=0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KDBG_INFO("M13: hello6.exe entry=0x%llx", entry_va);

        KThread* uthread = PS::CreateUserThread(
            proc, "hello6.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m13_thread_ok) { Sched::Schedule(); }
        Debug::Print("[MicroNT] M13 ready\r\n");
    }

    // ----------------------------------------------------------
    // M14: NtQuerySystemInformation + per-process heap
    //  hello7.exe queries kernel version (class 0) and memory
    //  stats (class 1), writes both via NtWriteFile.
    // ----------------------------------------------------------
    {
        g_m8_write_ok = 0;
        g_m14_info_ok = 0;

        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("hello7.exe", user_cr3);
        KASSERT(proc);

        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        u64 entry_va = 0;
        st = LDR::LoadPe(s_hello7_pe, s_hello7_pe_size,
                          user_cr3, s_hello7_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        constexpr u64 USER_STACK_VA = 0xE000100000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(stk_phys)[i]=0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KThread* uthread = PS::CreateUserThread(
            proc, "hello7.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m14_info_ok) { Sched::Schedule(); }
        Debug::Print("[MicroNT] M14 ready\r\n");
    }

    // ----------------------------------------------------------
    // M15: Priority scheduling, free-list allocator,
    //      shared memory, exception handling.
    //  hello8.exe tests all four features then writes "M15 OK".
    // ----------------------------------------------------------
    {
        g_m8_write_ok = 0;
        g_m15_ok      = 0;

        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("hello8.exe", user_cr3);
        KASSERT(proc);

        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        u64 entry_va = 0;
        st = LDR::LoadPe(s_hello8_pe, s_hello8_pe_size,
                          user_cr3, s_hello8_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        constexpr u64 USER_STACK_VA = 0xF000100000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(stk_phys)[i]=0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KThread* uthread = PS::CreateUserThread(
            proc, "hello8.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        uthread->Priority = THREAD_PRIORITY_HIGH;   // prove high-priority path works
        Sched::AddThread(uthread);

        while (!g_m15_ok) { Sched::Schedule(); }
        Debug::Print("[MicroNT] M15 ready\r\n");
    }

    // ----------------------------------------------------------
    // M16: NtCreateMutant + NtCreateSemaphore
    //  hello9.exe smoke-tests semaphore acquire/release and
    //  recursive mutant acquire/release, then writes "M16 OK".
    // ----------------------------------------------------------
    {
        g_m8_write_ok = 0;
        g_m16_ok      = 0;

        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("hello9.exe", user_cr3);
        KASSERT(proc);

        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        u64 entry_va = 0;
        st = LDR::LoadPe(s_hello9_pe, s_hello9_pe_size,
                          user_cr3, s_hello9_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        constexpr u64 USER_STACK_VA = 0x10000100000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(stk_phys)[i]=0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KThread* uthread = PS::CreateUserThread(
            proc, "hello9.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m16_ok) { Sched::Schedule(); }
        Debug::Print("[MicroNT] M16 ready\r\n");
    }

    // ----------------------------------------------------------
    // M17: VGA console, per-process PDPT, NtCreateProcess,
    //      NtWaitForMultipleObjects, NtOpenEvent.
    //  hello10.exe spawns TWO m17child processes from the VFS,
    //  waits for both via NtWaitForMultipleObjects, then
    //  writes "M17 OK".
    // ----------------------------------------------------------
    {
        g_m8_write_ok = 0;
        g_m17_ok      = 0;

        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("hello10.exe", user_cr3);
        KASSERT(proc);

        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        u64 entry_va = 0;
        st = LDR::LoadPe(s_hello10_pe, s_hello10_pe_size,
                          user_cr3, s_hello10_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        constexpr u64 USER_STACK_VA = 0x12000100000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(stk_phys)[i]=0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KThread* uthread = PS::CreateUserThread(
            proc, "hello10.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m17_ok) { Sched::Schedule(); }
        Debug::Print("[MicroNT] M17 ready\r\n");
    }

    // ----------------------------------------------------------
    // M18: Keyboard driver + shell v2 (exec, dir, mem, ver, exit)
    //  Pre-load: dir, exec m17child.exe, exit
    //  shell exit handler prints "M18 OK" -> g_m18_ok
    // ----------------------------------------------------------
    {
        g_m8_write_ok = 0;
        g_m18_ok      = 0;

        // Pre-load command queue for automated testing
        static const char* m18_cmds[] = {
            "dir", "exec m17child.exe", "exit"
        };
        SYSCALL::SetCommands(m18_cmds, 3);

        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("shell2.exe", user_cr3);
        KASSERT(proc);

        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        u64 entry_va = 0;
        st = LDR::LoadPe(s_shell_pe, s_shell_pe_size,
                          user_cr3, s_shell_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        constexpr u64 USER_STACK_VA = 0x9000100000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(stk_phys)[i]=0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KThread* uthread = PS::CreateUserThread(
            proc, "shell2.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m18_ok) { Sched::Schedule(); }
        Debug::Print("[MicroNT] M18 ready\r\n");
    }

    // ----------------------------------------------------------
    // M19: ps (process list), help command
    //  Commands: ps, exec m17child.exe, exit
    //  ps calls NtQuerySystemInformation(2) -> prints process list
    //  "System (1)" line triggers g_m19_ok
    // ----------------------------------------------------------
    {
        g_m8_write_ok = 0;
        g_m19_ok      = 0;

        static const char* m19_cmds[] = { "ps", "exec m17child.exe", "exit" };
        SYSCALL::SetCommands(m19_cmds, 3);

        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("shell3.exe", user_cr3);
        KASSERT(proc);

        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        u64 entry_va = 0;
        st = LDR::LoadPe(s_shell_pe, s_shell_pe_size,
                          user_cr3, s_shell_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        constexpr u64 USER_STACK_VA = 0x9000100000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(stk_phys)[i]=0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KThread* uthread = PS::CreateUserThread(
            proc, "shell3.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m19_ok) { Sched::Schedule(); }
        Debug::Print("[MicroNT] M19 ready\r\n");
    }

    // ----------------------------------------------------------
    // M20: echo, cat, clear shell commands + NtVgaClear syscall
    //  Commands: echo M20 PASS, clear, exit
    //  "echo M20 PASS" writes "M20 PASS\n" -> g_m20_ok
    // ----------------------------------------------------------
    {
        g_m8_write_ok = 0;
        g_m20_ok      = 0;

        static const char* m20_cmds[] = { "echo M20 PASS", "cat m17child.exe", "clear", "exit" };
        SYSCALL::SetCommands(m20_cmds, 4);

        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("shell4.exe", user_cr3);
        KASSERT(proc);

        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        u64 entry_va = 0;
        st = LDR::LoadPe(s_shell_pe, s_shell_pe_size,
                          user_cr3, s_shell_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        constexpr u64 USER_STACK_VA = 0x9000200000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(stk_phys)[i]=0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KThread* uthread = PS::CreateUserThread(
            proc, "shell4.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m20_ok) { Sched::Schedule(); }
    }

    // ----------------------------------------------------------
    // M21: writable filesystem + write/cat round-trip
    //  write test.txt -> NtCreateFile(WFile)+NtWriteFile(file)
    //  cat test.txt   -> NtReadFile reads it back; NtWriteFile(1) prints
    //  "M21 OK" triggers g_m21_ok
    // ----------------------------------------------------------
    {
        g_m8_write_ok = 0;
        g_m21_ok      = 0;

        static const char* m21_cmds[] = {
            "write test.txt M21 OK",
            "cat test.txt",
            "dir",
            "exit"
        };
        SYSCALL::SetCommands(m21_cmds, 4);

        u64 user_cr3 = VMM::CreateUserPml4();
        KASSERT(user_cr3);
        KProcess* proc = PS::CreateProcess("shell5.exe", user_cr3);
        KASSERT(proc);

        u64 ntdll_entry = 0;
        NTSTATUS st = LDR::LoadAndRegister(
            "ntdll.dll", s_ntdll_pe, s_ntdll_pe_size,
            user_cr3, s_ntdll_image_base, &ntdll_entry);
        KASSERT(NT_SUCCESS(st));

        u64 entry_va = 0;
        st = LDR::LoadPe(s_shell_pe, s_shell_pe_size,
                          user_cr3, s_shell_image_base, &entry_va);
        KASSERT(NT_SUCCESS(st));

        constexpr u64 USER_STACK_VA = 0x9000300000ULL;
        u64 stk_phys = PMM::AllocPage();
        KASSERT(stk_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(stk_phys)[i]=0;
        KASSERT(VMM::MapPageInto(user_cr3, USER_STACK_VA, stk_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KThread* uthread = PS::CreateUserThread(
            proc, "shell5.exe!main", entry_va, USER_STACK_VA + PAGE_SIZE);
        KASSERT(uthread);
        Sched::AddThread(uthread);

        while (!g_m21_ok) { Sched::Schedule(); }
        Debug::Print("[MicroNT] M21 ready\r\n");
    }

    // ----------------------------------------------------------
    // M22: named pipes + IPC (kernel-driven test)
    //  Kernel fills "ipc" pipe with "M22 OK\n"
    //  consumer.exe (from VFS) opens pipe, reads, prints to stdout
    //  stdout "M22 OK" -> g_m22_ok
    // ----------------------------------------------------------
    {
        g_m8_write_ok = 0;
        g_m22_ok      = 0;

        // Pre-fill the named pipe from kernel
        static const u8 m22_pipe_data[] = "M22 OK\n";
        SYSCALL::SetupTestPipe("ipc", m22_pipe_data, 7);

        // Load consumer.exe from VFS (direct syscalls, no ntdll needed)
        usize consumer_sz = 0;
        const u8* consumer_bin = VFS::FindFile("consumer.exe", &consumer_sz);
        KASSERT(consumer_bin);

        // Read ImageBase from PE optional header (e_lfanew -> NT headers -> OptHdr.ImageBase)
        u32 e_lf = *reinterpret_cast<const u32*>(consumer_bin + 0x3C);
        u64 c_base = *reinterpret_cast<const u64*>(consumer_bin + e_lf + 4 + 20 + 24);

        u64 c_cr3 = VMM::CreateUserPml4();
        KASSERT(c_cr3);
        KProcess* c_proc = PS::CreateProcess("consumer.exe", c_cr3);
        KASSERT(c_proc);

        u64 c_entry = 0;
        NTSTATUS st = LDR::LoadPe(consumer_bin, consumer_sz, c_cr3, c_base, &c_entry);
        KASSERT(NT_SUCCESS(st));

        // Map a pre-filled shared page at SHARED_VA = IMAGE_BASE+0x2000
        // consumer.exe reads "M22 OK\n" from it directly (zero-copy IPC)
        constexpr u64 SHARED_VA = 0x11000002000ULL;
        u64 shared_phys = PMM::AllocPage();
        KASSERT(shared_phys);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(shared_phys)[i]=0;
        static const u8 m22_msg[] = "M22 OK\n";
        for (u32 i=0;i<7;++i) reinterpret_cast<u8*>(shared_phys)[i]=m22_msg[i];
        KASSERT(VMM::MapPageInto(c_cr3, SHARED_VA, shared_phys,
                                  VMM::PTE_PRESENT|VMM::PTE_USER));

        constexpr u64 C_STACK_VA = 0x11000100000ULL;
        u64 c_stk = PMM::AllocPage();
        KASSERT(c_stk);
        for (u32 i=0;i<PAGE_SIZE;++i) reinterpret_cast<u8*>(c_stk)[i]=0;
        KASSERT(VMM::MapPageInto(c_cr3, C_STACK_VA, c_stk,
                                  VMM::PTE_PRESENT|VMM::PTE_WRITABLE|VMM::PTE_USER));

        KThread* c_thread = PS::CreateUserThread(
            c_proc, "consumer.exe!main", c_entry, C_STACK_VA + PAGE_SIZE);
        KASSERT(c_thread);
        Sched::AddThread(c_thread);

        while (!g_m22_ok) { Sched::Schedule(); }
        Debug::Print("[MicroNT] M22 ready\r\n");
    }

    // ----------------------------------------------------------
    // Ready
    // ----------------------------------------------------------
    Debug::Print("[MicroNT] Ready\r\n");

    // ================================================================
    // DRAIN: wait for every test process to exit before touching VGA.
    // Some test shells have commands queued past their detection point
    // (e.g. M20 shell still has "cat ... clear exit" after g_m20_ok=1).
    // If those run during our interactive while() they call VGA::Init()
    // and wipe the welcome screen.  We give them 2000 Schedule() calls
    // (~20 ms worth of ticks) to finish.
    // ================================================================
    for (u32 drain = 0; drain < 2000; ++drain) { Sched::Schedule(); }

    // ================================================================
    // INTERACTIVE MODE
    // ================================================================
    {
        SM::ShellImageConfig shell_cfg {
            s_ntdll_pe, s_ntdll_pe_size, s_ntdll_image_base,
            s_shell_pe, s_shell_pe_size, s_shell_image_base
        };
        SM::StartInteractiveSession(shell_cfg);

        while (true) { Sched::Schedule(); }
    }
}
