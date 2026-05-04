# Building MicroNT on Windows

## Prerequisites

| Tool | Minimum Version | Install |
|------|-----------------|---------|
| CMake | 3.20 | `scoop install cmake` |
| Ninja | 1.10 | `scoop install ninja` |
| NASM | 2.15 | `scoop install nasm` |
| LLVM/Clang | 16 | `scoop install llvm` |
| ld.lld | 16 (bundled with LLVM) | — |
| MSYS2 + grub | any | See below |
| xorriso | any | Via MSYS2 |
| VirtualBox | 7.x | https://virtualbox.org |

### Quick install via Scoop

```powershell
# Install Scoop if not present
Set-ExecutionPolicy RemoteSigned -Scope CurrentUser
irm get.scoop.sh | iex

scoop install cmake ninja nasm llvm
```

### MSYS2 (grub-mkrescue + xorriso)

1. Download from https://www.msys2.org/
2. Open MSYS2 UCRT64:
   ```bash
   pacman -Syu
   pacman -S grub xorriso
   ```
3. Add `C:\msys64\usr\bin` to `PATH`.

## Build Steps

```powershell
# From repo root:
.\scripts\build.ps1       # builds artifacts/micront.elf
.\scripts\make-iso.ps1    # builds artifacts/MicroNT.iso
```

## Environment Variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `LLVM_PATH` | Path to LLVM bin dir | Search PATH |
| `NASM_PATH` | Path to nasm.exe | Search PATH |
| `CMAKE_PATH` | Path to cmake.exe | Search PATH |
| `VBOXMANAGE_PATH` | Path to VBoxManage.exe | Default install |

## Output Files

```
artifacts/
  micront.elf    — kernel ELF binary
  MicroNT.iso    — bootable ISO for VirtualBox
  serial.log     — serial output (populated when VM runs)
```

## Troubleshooting

**`clang: error: unsupported option '--target=...'`**
→ Ensure LLVM ≥ 16 is installed and `clang` in PATH is the LLVM version, not Apple Clang.

**`ld.lld not found`**
→ Add LLVM bin directory to PATH: `scoop prefix llvm`

**`grub-mkrescue: command not found`**
→ Add MSYS2 usr/bin to PATH, or run from MSYS2 shell.

**CMake tries to use MSVC instead of Clang**
→ The toolchain file forces Clang. If CMake caches an old config, run `.\scripts\clean.ps1` first.
