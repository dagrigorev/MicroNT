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
    // 6. VMM stub (identity map from UEFI bootloader in effect)
    // ----------------------------------------------------------
    VMM::Init();
    Debug::Print("[MicroNT] Virtual memory manager initialized\r\n");

    // ----------------------------------------------------------
    // 7. Object manager
    // ----------------------------------------------------------
    OB::Init();
    Debug::Print("[MicroNT] Object manager initialized\r\n");

    // ----------------------------------------------------------
    // 8. Process manager
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
    // Verify timer is actually ticking (wait up to 500 ms)
    // ----------------------------------------------------------
    {
        u64 t0 = HAL::PitTicks();
        HAL::PitSleep(200);   // wait 200 ms
        u64 t1 = HAL::PitTicks();
        u64 delta = t1 - t0;
        Debug::Printf("[INFO ] Timer: %llu ticks in 200 ms (expected ~20)\r\n", delta);
        if (delta >= 10) {
            Debug::Print("[MicroNT] M2 ready\r\n");
        } else {
            Debug::Printf("[WARN ] Timer tick count low (%llu) - IRQ0 may not be firing\r\n", delta);
        }
    }

    // ----------------------------------------------------------
    // Ready
    // ----------------------------------------------------------
    Debug::Print("[MicroNT] Ready\r\n");

    HAL::DisableInterrupts();
    for (;;) __asm__ volatile("hlt");
}
