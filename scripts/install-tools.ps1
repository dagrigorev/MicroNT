#Requires -Version 5.1
<#
.SYNOPSIS
    Install MicroNT build dependencies on Windows.
    ISO creation uses tools/mkiso.py (pure Python) - no xorriso needed.
#>

Set-StrictMode -Version Latest

Write-Host "=== MicroNT Tool Installer ===" -ForegroundColor Cyan

# ?? Python 3 ?????????????????????????????????????????????????
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Host "Python 3 not found. Install via winget:" -ForegroundColor Yellow
    Write-Host "  winget install Python.Python.3"
    Write-Host "Or download from https://www.python.org/downloads/"
} else {
    $v = & python --version 2>&1
    Write-Host "[OK] Python: $v" -ForegroundColor Green
}

# ?? Scoop ?????????????????????????????????????????????????????
if (-not (Get-Command scoop -ErrorAction SilentlyContinue)) {
    Write-Host "Installing Scoop..." -ForegroundColor Yellow
    Set-ExecutionPolicy RemoteSigned -Scope CurrentUser -Force
    Invoke-RestMethod get.scoop.sh | Invoke-Expression
}

# ?? Build tools (C compiler / assembler / build system) ??????
foreach ($t in @('cmake', 'ninja', 'nasm', 'llvm')) {
    Write-Host "scoop install $t ..." -ForegroundColor Cyan
    scoop install $t
}

Write-Host @"

=== VirtualBox ===
Download VirtualBox from https://virtualbox.org
Enable EFI firmware - create-vbox-vm.ps1 does this automatically.

=== Build workflow ===
  .\scripts\build.ps1           # kernel ELF + UEFI bootloader
  .\scripts\make-iso.ps1        # MicroNT.iso  (pure Python, no extra tools)
  .\scripts\create-vbox-vm.ps1  # create EFI VM (one-time)
  .\scripts\run-virtualbox.ps1  # boot
  .\scripts\debug-serial.ps1    # watch COM1 output

See docs/BUILDING.md for details.
"@
