#Requires -Version 5.1
<#
.SYNOPSIS
    Build a UEFI-bootable ISO using tools/mkiso.py (pure Python 3).
    No external ISO tools required - Python 3 is the only dependency.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root      = Split-Path $PSScriptRoot -Parent
$Artifacts = Join-Path $Root 'artifacts'
$MkIso     = Join-Path $Root 'tools\mkiso.py'
$EfiBoot   = Join-Path $Artifacts 'BOOTX64.EFI'
$KernelElf = Join-Path $Artifacts 'micront.elf'
$IsoOut    = Join-Path $Artifacts 'MicroNT.iso'

# ?? Pre-flight checks ????????????????????????????????????????
foreach ($f in @($EfiBoot, $KernelElf)) {
    if (-not (Test-Path $f)) {
        Write-Host "[ERROR] Missing: $f" -ForegroundColor Red
        Write-Host "        Run .\scripts\build.ps1 first."
        exit 1
    }
}

$python = Get-Command 'python' -ErrorAction SilentlyContinue
if (-not $python) {
    $python = Get-Command 'python3' -ErrorAction SilentlyContinue
}
if (-not $python) {
    Write-Host "[ERROR] Python 3 not found." -ForegroundColor Red
    Write-Host "        Install from https://www.python.org/downloads/" -ForegroundColor Yellow
    Write-Host "        or: winget install Python.Python.3"
    exit 1
}
Write-Host "[OK] Python -> $($python.Source)" -ForegroundColor Green

# ?? Run mkiso.py ?????????????????????????????????????????????
Write-Host "Building ISO..."
& $python.Source $MkIso $EfiBoot $KernelElf $IsoOut
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] mkiso.py failed (exit $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

$sizeMB = [math]::Round((Get-Item $IsoOut).Length / 1MB, 2)
Write-Host "[SUCCESS] $IsoOut ($sizeMB MB)" -ForegroundColor Green

# Auto-attach to VM if already configured
$UuidFile = Join-Path $Root 'artifacts\.vmuuid'
if (Test-Path $UuidFile) {
    Write-Host 'VM is configured - re-attaching ISO...' -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot 'attach-iso.ps1')
} else {
    Write-Host '  Next: .\scripts\create-vbox-vm.ps1'
}
