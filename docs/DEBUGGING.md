# MicroNT Debugging Guide

## Serial Debug Output

All kernel debug output goes to COM1 (0x3F8) at 115200 baud, 8N1.

VirtualBox captures COM1 to `artifacts/serial.log`.

Watch live:
```powershell
.\scripts\debug-serial.ps1
```

## Debug Macros

```cpp
KDBG_INFO(fmt, ...)   // [INFO ] prefix
KDBG_WARN(fmt, ...)   // [WARN ] prefix
KDBG_ERROR(fmt, ...)  // [ERROR] prefix
KDBG_TRACE(fmt, ...)  // [TRACE] prefix
KASSERT(condition)    // panics with file/line if false
```

Supported printf specifiers: `%s %c %d %u %x %X %llx %llu %p`

## Kernel Panic Format

```
=======================================================
  MicroNT KERNEL PANIC
=======================================================
  <message>
=======================================================
System halted.
```

## Exception Output Format

```
[EXCEPTION] General Protection (#13) at RIP=0xFFFF800000101234 err=0x0
  RAX=0x0000000000000000 RBX=0x... RCX=0x...
  RDX=0x... RSI=0x... RDI=0x...
  RSP=0x... RBP=0x... RFLAGS=0x...
```

Page fault:
```
[EXCEPTION] #PF at RIP=0x... CR2=0x0000DEAD00000000 err=0x6
  not-present write user
```

## GDB (Optional, Future)

TODO(M5): VirtualBox serial GDB stub integration for source-level debugging.

For now, all debugging is via serial printf.

## Common Issues

| Symptom | Cause | Fix |
|---------|-------|-----|
| Kernel panics on boot | GDT/IDT setup error | Check `gdt.cpp`, verify selector 0x08 for code |
| Triple fault loop | Page table error | Ensure boot.asm PD entries use `0x83` (huge+present+writable) |
| Serial log empty | VirtualBox UART not configured | Re-run `create-vbox-vm.ps1` |
| `KASSERT` fires on PMM | Memory map not parsed | Confirm VirtualBox VM has ≥ 256 MB RAM |
