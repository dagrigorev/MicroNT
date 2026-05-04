# MicroNT Architecture

## Overview

MicroNT is structured as a monolithic kernel with NT-inspired subsystem boundaries. Each major component is isolated behind a C++ namespace that will eventually become a real module boundary.

```
┌──────────────────────────────────────────────────────────┐
│                    User Mode (Ring 3)                     │
│   hello.exe  console-test.exe  shell.exe                  │
│   ─────────────────────────────────────────────────────   │
│   kernel32.dll     user32.dll (stub)                      │
│   ntdll.dll  (syscall stubs + native runtime)             │
└─────────────────────────┬────────────────────────────────┘
                          │ SYSCALL / SYSRET
┌─────────────────────────▼────────────────────────────────┐
│                   Kernel Mode (Ring 0)                    │
│  ┌───────────┐  ┌──────────┐  ┌────────────┐             │
│  │  SYSCALL  │  │    PS    │  │     OB     │             │
│  │  Layer    │  │ Process  │  │  Object    │             │
│  │ (M6)      │  │ Manager  │  │  Manager   │             │
│  └───────────┘  └──────────┘  └────────────┘             │
│  ┌───────────┐  ┌──────────┐  ┌────────────┐             │
│  │    LDR    │  │    MM    │  │     IO     │             │
│  │ PE Loader │  │  Memory  │  │  Manager   │             │
│  │           │  │ Manager  │  │            │             │
│  └───────────┘  └──────────┘  └────────────┘             │
│  ┌──────────────────────────────────────────┐             │
│  │                  HAL                     │             │
│  │  GDT  IDT  PIC  Serial  CPU  VMM(paging) │             │
│  └──────────────────────────────────────────┘             │
└──────────────────────────────────────────────────────────┘
                          │
             ┌────────────▼──────────────┐
             │     VirtualBox (x86_64)   │
             │  BIOS boot → GRUB2 → ELF  │
             └───────────────────────────┘
```

## Boot Flow

1. BIOS loads GRUB2 from ISO
2. GRUB2 loads `micront.elf` via Multiboot2
3. `boot.asm` runs in 32-bit protected mode:
   - Builds temporary identity-map page tables (first 1 GB, 2 MB huge pages)
   - Enables long mode + paging
   - Far-jumps to 64-bit code
4. `kernel_main()` in C++ takes over

## Key Subsystems

### HAL (Hardware Abstraction Layer)
- COM1 serial driver (115200 8N1)
- GDT: null, kernel code/data, user code/data, TSS
- IDT: 48 entries (32 exceptions + 16 IRQs)
- 8259A PIC remapped to INT 0x20–0x2F
- Inline I/O port and MSR helpers

### PMM (Physical Memory Manager)
- Bitmap allocator (1 bit per 4 KB page)
- Supports up to 8 GB
- Parses Multiboot2 memory map (tag type 6)
- Reserves first 2 MB for kernel + low memory

### KernelHeap
- M1: simple bump allocator
- M3: will be replaced with a proper slab/buddy allocator

### VMM (Virtual Memory Manager)
- M1: stub, identity map from boot.asm in use
- M3: CR3 management, MapPage/UnmapPage, page fault handler

### OB (Object Manager)
- Every object has an `ObjectHeader` (magic, type, refcount, name)
- M1: `AllocateObject` backed by kernel heap
- M4: handle tables, object namespace

### PS (Process Manager)
- `KProcess` and `KThread` structures
- M1: single system process stub
- M5: real address spaces, ring-3 transition, scheduler

### LDR (PE Loader)
- Validates DOS header, NT headers, machine type, PE32+
- Copies sections, applies base relocations
- M7: import resolution from initrd DLLs

### IO Manager
- M1: console backed by COM1 serial
- M4: device objects, IRP-like request structures

## Memory Map (M1)

```
0x0000_0000 – 0x0009_FFFF   Low memory, BDA/EBDA
0x000A_0000 – 0x000F_FFFF   VGA + ROM
0x0010_0000 – kernel_end     MicroNT kernel ELF
kernel_end  – +4 MB          Kernel heap
...                          Free physical pages (PMM)
```

## Initrd Archive Format (MNTAR001)

Planned for M7. Simple sequential format:

```
Offset  Field
0       Magic: "MNTAR001" (8 bytes)
8       FileCount: u32
12      [FileEntry × N]:
          PathOffset: u32   (offset into string table)
          DataOffset: u32   (from archive start)
          DataSize:   u32
          Flags:      u32   (0=file, 1=directory)
...     String table (null-terminated paths)
...     File data
```
