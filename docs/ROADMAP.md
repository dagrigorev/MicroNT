# MicroNT Roadmap

## Milestone Status

| # | Name | Status | Key Deliverables |
|---|------|--------|-----------------|
| M0 | Build foundation | ✅ Done | CMake, Ninja, Clang toolchain, PowerShell scripts |
| M1 | Bootable kernel | ✅ Done | boot.asm, long mode, GDT, IDT, PIC, PMM, heap stubs, serial debug |
| M2 | HAL & interrupts | 🔲 Next | PIT timer, IRQ dispatch, exception details, spinlock |
| M3 | Full VMM | 🔲 | CR3 management, MapPage/UnmapPage, page fault handler, user/kernel split |
| M4 | Object manager | 🔲 | Handle tables, per-process handle space, object namespace |
| M5 | Process/thread | 🔲 | KProcess/KThread, round-robin scheduler, ring-3 transition |
| M6 | Syscall layer | 🔲 | SYSCALL/SYSRET, NtWriteFile, NtTerminateProcess, etc. |
| M7 | PE loader | 🔲 | Import resolution, DLL loading from initrd, TLS/SEH stubs |
| M8 | Win32 compat | 🔲 | ntdll.dll, kernel32.dll, WriteFile, GetStdHandle, VirtualAlloc |
| M9 | Console shell | 🔲 | STDIN/STDOUT handles, basic shell commands |
| M10| Test suite | 🔲 | Unit tests, PE smoke tests, compatibility matrix |

## M2 Plan

- `hal/x64/pit.cpp`: PIT channel 0 → IRQ0 at ~100 Hz
- `hal/x64/idt.cpp`: enable IRQ0 in PIC mask
- `kernel/core/scheduler.cpp`: stub tick counter
- Divide-by-zero test in `kernel_main` to verify exception output

## M3 Plan

- `mm/virtual_memory.cpp`: `MapPage(virt, phys, flags)`, `UnmapPage(virt)`
- Kernel loaded at `0xFFFFFFFF80100000` (higher half)
- Identity map removed after kernel re-map
- Page fault (#14) handler: demand-zero for kernel heap, panic for unmapped

## M5 Plan

- `IRETQ` frame setup for ring-3 entry
- User stack allocated via VMM in user region (`0x0000_0000_0040_0000` upward)
- Minimal `SYSCALL MSR` setup (STAR, LSTAR, SFMASK)
- Round-robin `Ready` queue (single CPU)

## Compatibility Targets

### Phase 1 (M8)
- Simple console PE compiled with MSVC/Clang
- Uses: `WriteConsoleA`, `ExitProcess`, `GetStdHandle`

### Phase 2 (M10)
- Basic CRT startup: `main()` / `WinMain()`
- `malloc` / `free` via `VirtualAlloc`
- `printf` via console write

### Phase 3 (future)
- File I/O on virtual FAT volume
- Window manager stub (graphical framebuffer)
- Networking via VirtIO or NE2000 emulation
