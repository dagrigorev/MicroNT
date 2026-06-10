# VirtualBox Setup for MicroNT

## VM Configuration

| Setting | Value |
|---------|-------|
| Name | MicroNT-Test |
| OS Type | Other/Unknown (64-bit) |
| RAM | 512 MB |
| CPUs | 1 |
| Firmware | EFI |
| Boot order | DVD first |
| Serial COM1 | Enabled, output to file |
| Audio | Disabled |
| USB | Disabled |

## Display resolution / screen auto-fit

The desktop fills whatever resolution the firmware presents. At boot the
UEFI bootloader (`SelectBestGopMode`) honors the EFI graphics mode and only
overrides it if it is tiny or larger than the safe ceiling (1920x1200). In
VirtualBox that mode is controlled by an extradata key, so to fit the guest
desktop to the size you want:

```powershell
VBoxManage setextradata MicroNT-Test "VBoxInternal2/EfiGraphicsResolution" "1600x900"
```

The VM-creation scripts set this to `1920x1080` by default. Verified working
at 1280x720, 1600x900 and 1920x1080; the mouse range and centered taskbar
scale to the chosen size automatically.

When the adapter is `vmsvga`, MicroNT also loads a **VMware SVGA II driver**
(`kernel/hal/x64/vmsvga.cpp`): it discovers the device on PCI, owns the linear
framebuffer, sets the mode at runtime (`VMSVGA::SetMode`), and flushes frames
over the command FIFO (`VMSVGA::Present`). The kernel therefore controls the
display itself rather than depending on the pre-boot GOP mode; it falls back
to GOP automatically on hardware without the SVGA II device.

**Limitation:** live resize while the VM runs (dragging the window so the
guest reflows) still needs the VirtualBox resize-hint path (HGSMI/VBVA: a
shared area + IRQ that reports the host's preferred size). The SVGA II driver
is the foundation for that; wiring the resize hint on top is the next step.
For now set the resolution before booting via the extradata key above.

## Using the Script

```powershell
.\scripts\create-vbox-vm.ps1   # create VM
.\scripts\run-virtualbox.ps1   # start VM
.\scripts\debug-serial.ps1     # tail serial log
```

## Manual VBoxManage Commands

```bat
rem Create VM
VBoxManage createvm --name MicroNT-Test --ostype Other_64 --register
VBoxManage modifyvm MicroNT-Test --memory 512 --cpus 1 --boot1 dvd --firmware efi
VBoxManage modifyvm MicroNT-Test --uart1 0x3F8 4 --uartmode1 file artifacts\serial.log

rem Attach ISO
VBoxManage storagectl MicroNT-Test --name IDE --add ide
VBoxManage storageattach MicroNT-Test --storagectl IDE --port 0 --device 0 ^
    --type dvddrive --medium artifacts\MicroNT.iso

rem Start
VBoxManage startvm MicroNT-Test --type gui

rem Stop
VBoxManage controlvm MicroNT-Test poweroff
```

## Expected Serial Output (M1)

```
[MicroNT] Boot started
[MicroNT] CPU: x86_64 long mode (mb_info @ 0x...)
[INFO ] CPU vendor: GenuineIntel
[INFO ] CPU brand:  Intel(R) Core(TM) ...
[MicroNT] GDT initialized
[MicroNT] IDT initialized
[MicroNT] HAL initialized
[INFO ] [PMM] Physical memory: 512 MB total, NNN MB free
[MicroNT] Physical memory manager initialized
[INFO ] KernelHeap: base=0x... size=4096 KB
[INFO ] VMM: M1 stub — identity map from boot.asm active
[MicroNT] Virtual memory manager initialized
[INFO ] OB: Object manager initialized (M1 stub)
[MicroNT] Object manager initialized
[INFO ] PS: Process manager initialized (M1 stub)
[MicroNT] Process manager initialized
[INFO ] IO: I/O manager initialized (M1 stub)
[INFO ] IO: Console initialized (serial-backed)
[INFO ] SYSCALL: layer initialized (M1 stub)
[INFO ] LDR: PE loader initialized (M1 stub)
[MicroNT] PE loader initialized
[MicroNT] Initrd mounted (TODO: parse MNTAR001)
[MicroNT] Ready
```

## Troubleshooting

**Black screen / no serial output**
- Confirm boot order is DVD first
- Confirm ISO was generated correctly (`artifacts/MicroNT.iso` exists and is > 0 bytes)
- Enable VirtualBox debug log: Machine → Settings → General → Advanced → Log folder

**Triple fault / reboot loop**
- Usually a GDT or IDT error
- Check that NASM and Clang versions are supported
- Try disabling optimization: add `-O0` to CMakeLists.txt compile flags

**Serial log file is empty**
- Confirm `--uartmode1 file` was set (check VM settings in VirtualBox GUI)
- The file is created when the VM starts; content appears as the kernel writes

**GRUB `unknown filesystem` error**
- Rebuild the ISO with `.\scripts\make-iso.ps1`
- Confirm xorriso is installed and in PATH
