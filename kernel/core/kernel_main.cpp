// kernel_main.cpp - MicroNT kernel entry point (UEFI path)
// Called from boot.asm (_kernel_start) after convention conversion.

#include "../include/ntdef.h"
#include "../include/bootinfo.h"
#include "../include/ntstatus.h"
#include "../include/debug.h"
#include "../include/hal.h"
#include "../include/memory.h"
#include "../include/object.h"
#include "../include/process.h"
#include "../include/pe.h"
#include "../include/io.h"

extern "C" void kernel_main(MicroNTBootInfo* boot_info) {
    // ----------------------------------------------------------
    // 1. Serial + debug (first so we can log everything)
    // ----------------------------------------------------------
    Serial::Init(COM1_PORT, DEFAULT_BAUD);
    Debug::Init();

    Debug::Print("\r\n");
    Debug::Print("[MicroNT] Boot started\r\n");
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

    // ----------------------------------------------------------
    // 10. Syscall layer
    // ----------------------------------------------------------
    SYSCALL::Init();

    // ----------------------------------------------------------
    // 11. PE loader
    // ----------------------------------------------------------
    LDR::Init();
    Debug::Print("[MicroNT] PE loader initialized\r\n");

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
        extern volatile u32 g_m6_syscall_ok;

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
    // Ready
    // ----------------------------------------------------------
    Debug::Print("[MicroNT] Ready\r\n");

    HAL::DisableInterrupts();
    for (;;) __asm__ volatile("hlt");
}
