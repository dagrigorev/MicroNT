# MicroNT

A minimal NT-inspired OS kernel that boots on real x86-64 hardware and VirtualBox.
Written from scratch in C++20 and NASM. No Windows source, no ReactOS, no EDK2.

## Status

**M0 + M1 complete and passing automated boot test.**

```
[MicroNT] Boot started
[MicroNT] GDT initialized
[MicroNT] IDT initialized
[MicroNT] HAL initialized
[PMM] Physical memory: 515 MB total, 491 MB free
[MicroNT] Physical memory manager initialized
[MicroNT] Virtual memory manager initialized
[MicroNT] Object manager initialized
[MicroNT] Process manager initialized
[MicroNT] PE loader initialized
[MicroNT] Ready
```

## Quick Start

```powershell
# 1. Install tools (one-time)
.\scripts\install-tools.ps1

# 2. Full automated build + boot test
.\scripts\setup-and-test.ps1

# 3. Or step by step:
.\scripts\build.ps1           # kernel ELF + UEFI bootloader
.\scripts\make-iso.ps1        # packages VHD disk image (pure Python, no extra tools)
.\scripts\create-vbox-vm.ps1  # create EFI VM (one-time)
.\scripts\run-virtualbox.ps1  # boot
.\scripts\debug-serial.ps1    # tail COM1 output
```

## Architecture

```
Host Windows
  clang (kernel ELF)  +  clang++ lld-link (UEFI PE bootloader)
  python tools/mkdisk.py  ->  MicroNT.vhd (MBR + FAT32 ESP)
  VirtualBox EFI  ->  /EFI/BOOT/BOOTX64.EFI  ->  kernel_main()
```

### Boot chain

```
VirtualBox UEFI firmware
  -> reads MBR partition type 0xEF
  -> mounts FAT32 EFI System Partition
  -> loads /EFI/BOOT/BOOTX64.EFI  (our UEFI bootloader, PE32+)
     -> locates /boot/micront.elf
     -> loads ELF segments at 0x100000
     -> builds identity-mapped page tables (0-4 GB, 2 MB huge pages)
     -> collects UEFI memory map + ACPI RSDP
     -> calls ExitBootServices
     -> jumps to _kernel_start (boot.asm)
        -> sets up kernel stack
        -> calls kernel_main(MicroNTBootInfo*)
           -> serial debug (COM1 115200 8N1)
           -> CPU detection (CPUID vendor/brand)
           -> GDT (null, kcode, kdata, ucode, udata, TSS)
           -> 8259A PIC (remapped to INT 0x20-0x2F)
           -> IDT (48 entries: 32 exceptions + 16 IRQs)
           -> PMM (bitmap allocator, parses UEFI memory map)
           -> kernel heap (4 MB bump allocator)
           -> VMM stub (identity map in effect)
           -> object manager stub
           -> process manager stub
           -> I/O manager + serial console
           -> SYSCALL layer stub
           -> PE loader stub
           -> halt loop
```

### Toolchain

| Component | Tool | Target |
|-----------|------|--------|
| Kernel | clang + ld.lld | x86_64-unknown-none-elf |
| UEFI bootloader | clang++ + lld-link | x86_64-unknown-windows (PE32+) |
| Boot assembly | NASM | elf64 / flat |
| Disk image | Python 3 (tools/mkdisk.py) | MBR + FAT32 VHD |
| Build system | CMake + Ninja | - |

### Key design decisions

- **No GRUB, no EDK2, no MSYS2** - entire toolchain available via `scoop`
- **VHD disk image** over ISO: VirtualBox EFI reliably boots GPT/MBR hard disks; CD-ROM El Torito support is incomplete in bundled OVMF
- **FAT32 with SPC=1**: FAT type is determined by cluster count (not the BPB string). A small partition with large clusters falls below the 65,525-cluster threshold and gets misidentified as FAT16. SPC=1 with a 64 MB disk gives ~127,000 clusters.
- **MBR partition type 0xEF**: VirtualBox UEFI's PartitionDxe creates child block-IO handles for type 0xEF, FatDxe mounts the FAT32, BDS finds BOOTX64.EFI
- **Packed GDT struct**: separating `GdtEntry[5]` and the TSS descriptor with `alignas(16)` inserts 8 bytes of padding, placing the TSS at offset 48 instead of 40; `ltr 0x28` reads garbage and triple-faults
- **Serial without loopback test**: the 16550 loopback test leaves the UART in loopback mode (MCR bit 4 set) when it fails; subsequent kernel output goes to the internal RX buffer, not to the physical TX line

## Milestones

| # | Name | Status | Key deliverables |
|---|------|--------|-----------------|
| M0 | Build foundation | DONE | CMake, Ninja, Clang toolchain, PowerShell scripts, Python VHD builder |
| M1 | Bootable kernel | DONE | UEFI bootloader, long mode, GDT/IDT/PIC, PMM, heap, serial debug |
| M2 | HAL + interrupts | DONE | PIT 100 Hz, IRQ dispatch table, spinlock, timer verification |
| M3 | Full VMM | next | CR3 management, MapPage/UnmapPage, page fault handler |
| M4 | Object manager | - | Handle tables, per-process handle space, object namespace |
| M5 | Process/thread | - | KProcess/KThread, round-robin scheduler, ring-3 transition |
| M6 | Syscall layer | - | SYSCALL/SYSRET, NtWriteFile, NtTerminateProcess |
| M7 | PE loader | - | Import resolution, DLL loading from initrd |
| M8 | Win32 compat | - | ntdll.dll, kernel32.dll, WriteFile, VirtualAlloc |
| M9 | Console shell | - | STDIN/STDOUT handles, basic commands |
| M10 | Test suite | - | Unit tests, PE smoke tests, compatibility matrix |

## Repository layout

```
boot/
  boot.asm              64-bit kernel entry stub (stack setup, ABI conversion)
  uefi/
    bootloader.cpp      UEFI EFI application
    uefi.h              Minimal UEFI types (no EDK2)
    elf64.h             ELF64 structs for kernel loader
kernel/
  include/              ntdef.h, ntstatus.h, bootinfo.h, hal.h, memory.h ...
  core/                 kernel_main.cpp, debug.cpp, panic.cpp
  hal/x64/              serial.cpp, cpu.cpp, gdt.cpp, idt.cpp, pic.cpp,
                        interrupts.asm
  mm/                   physical_memory.cpp, virtual_memory.cpp, heap.cpp
  ob/                   object_manager.cpp
  ps/                   process.cpp
  io/                   console.cpp
  ldr/                  pe_loader.cpp
  rtl/                  string.cpp
  syscall/              syscall.cpp
  linker.ld
  CMakeLists.txt
cmake/
  Toolchain-kernel.cmake
tools/
  mkdisk.py             Pure Python VHD builder (MBR + FAT32, 64 MB)
  mkiso.py              Pure Python ISO builder (El Torito, legacy)
  inspect_vhd.py        VHD/FAT structure diagnostic tool
scripts/
  setup-and-test.ps1    Full automated build + VM + boot test (CI entry point)
  build.ps1             Build kernel + bootloader
  make-iso.ps1          Package VHD (calls mkdisk.py)
  create-vbox-vm.ps1    Create EFI VirtualBox VM
  run-virtualbox.ps1    Start VM
  debug-serial.ps1      Tail COM1 serial log
  attach-iso.ps1        Attach VHD to existing VM
  install-tools.ps1     Install deps via scoop
docs/
  ARCHITECTURE.md
  BUILDING.md
  VIRTUALBOX.md
  ROADMAP.md
  DEBUGGING.md
  COMPATIBILITY.md
user/                   User-mode stubs (ntdll, kernel32, hello app)
artifacts/              Build outputs (gitignored)
  micront.elf
  BOOTX64.EFI
  MicroNT.vhd
  serial.log
```

## Requirements

| Tool | Install |
|------|---------|
| CMake 3.20+ | `scoop install cmake` |
| Ninja | `scoop install ninja` |
| NASM | `scoop install nasm` |
| LLVM/Clang 16+ | `scoop install llvm` |
| Python 3.8+ | https://python.org or `winget install Python.Python.3` |
| VirtualBox 7.x | https://virtualbox.org |

## License

Original work. No Windows source, ReactOS, Wine, or EDK2 code used.
