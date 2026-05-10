#Requires -Version 5.1
<#
.SYNOPSIS
    Build MicroNT and launch it in an interactive VirtualBox window.

.DESCRIPTION
    1. Build kernel + bootloader via build.ps1          (skip with -SkipBuild)
    2. Build a fresh VHD image (separate from test VHD)
    3. Create the 'MicroNT' VM if it does not exist yet,
       or reuse and reconfigure it if it does
    4. Start the VM with a display window

    IMPORTANT: Uses VBoxVGA graphics controller (not vmsvga/vmsvga2).
    MicroNT writes directly to the VGA text buffer at 0xB8000, which
    requires the legacy VBoxVGA controller to be visible on screen.

.PARAMETER SkipBuild
    Skip build.ps1 and reuse existing artifacts.

.PARAMETER VmName
    VirtualBox VM name. Default: MicroNT
#>
param(
    [switch]$SkipBuild = $false,
    [string]$VmName    = 'MicroNT'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---- Paths ------------------------------------------------------------------
$Root      = Split-Path $PSScriptRoot -Parent
$Artifacts = Join-Path $Root 'artifacts'
$EfiPath   = Join-Path $Artifacts 'BOOTX64.EFI'
$ElfPath   = Join-Path $Artifacts 'micront.elf'
$VhdPath   = Join-Path $Artifacts 'MicroNT-interactive.vhd'
$UuidFile  = Join-Path $Artifacts '.interactive-vmuuid'

if (-not (Test-Path $Artifacts)) {
    New-Item -ItemType Directory -Path $Artifacts | Out-Null
}

# ---- Helpers ----------------------------------------------------------------
function Log  ([string]$m, [string]$col = 'Cyan') { Write-Host "  $m" -ForegroundColor $col }
function Step ([string]$m) { Write-Host "`n--- $m ---" -ForegroundColor Yellow }
function Pass ([string]$m) { Write-Host "  [PASS] $m" -ForegroundColor Green  }
function Fail ([string]$m) { Write-Host "  [FAIL] $m" -ForegroundColor Red; exit 1 }

# ---- Find VBoxManage --------------------------------------------------------
$vbox = $null
foreach ($c in @(
    'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe',
    'C:\Program Files (x86)\Oracle\VirtualBox\VBoxManage.exe'
)) { if (Test-Path $c) { $vbox = $c; break } }
if (-not $vbox) {
    $v = Get-Command VBoxManage -ErrorAction SilentlyContinue
    if ($v) { $vbox = $v.Source }
}
if (-not $vbox) { Fail 'VBoxManage not found - install VirtualBox first.' }
Log "VBoxManage: $vbox" Green

function VBox {
    $out = & $vbox @args 2>&1
    if ($LASTEXITCODE -ne 0) { throw "VBoxManage $($args[0]) failed:`n$out" }
    $out
}

# ---- Find Python ------------------------------------------------------------
$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) { $py = Get-Command python3 -ErrorAction SilentlyContinue }
if (-not $py) { Fail 'python / python3 not found.' }

# ---- Step 1: Build ----------------------------------------------------------
if ($SkipBuild) {
    Log 'Skipping build (-SkipBuild specified).' Gray
    foreach ($f in @($EfiPath, $ElfPath)) {
        if (-not (Test-Path $f)) {
            Fail "Missing: $f  (run without -SkipBuild first)"
        }
    }
} else {
    Step 'Build kernel + bootloader'
    & (Join-Path $PSScriptRoot 'build.ps1') -Clean   # always clean to avoid stale .o files
    if ($LASTEXITCODE -ne 0) { Fail 'build.ps1 failed.' }
    Pass 'Kernel + bootloader built'
}

# ---- Step 2: Build VHD ------------------------------------------------------
Step 'Build VHD disk image'
& $py.Source (Join-Path $Root 'tools\mkdisk.py') $EfiPath $ElfPath $VhdPath 2>&1 |
    ForEach-Object { Log $_ Gray }
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $VhdPath)) {
    Fail 'mkdisk.py failed.'
}
$mb = [math]::Round((Get-Item $VhdPath).Length / 1MB, 1)
if ($mb -lt 60) { Fail "VHD too small ($mb MB)." }
Pass "VHD built: $mb MB"

# ---- Step 3: Create or reconfigure VM ---------------------------------------
Step "Prepare VM '$VmName'"

$UUID    = $null
$listed  = & $vbox list vms 2>&1 | Out-String

if ($listed -match [regex]::Escape("`"$VmName`"")) {
    # VM already registered
    $m = [regex]::Match($listed, [regex]::Escape("`"$VmName`"") + '\s+\{([0-9a-f-]+)\}')
    if ($m.Success) { $UUID = $m.Groups[1].Value }
    Log "Found existing VM  (UUID: $UUID)"

    # Must be powered off to change settings
    $info = & $vbox showvminfo $UUID --machinereadable 2>&1 | Out-String
    if ($info -match 'VMState="running"') {
        Log 'Powering off running VM...' Yellow
        & $vbox controlvm $UUID poweroff 2>&1 | Out-Null
        Start-Sleep 2
    }

    # Detach current disk
    & $vbox storageattach $UUID --storagectl SATA --port 0 --device 0 `
        --type hdd --medium none 2>&1 | Out-Null

    # Re-apply display settings (fix vmsvga -> VBoxVGA if needed)
    Log 'Applying display settings (VBoxVGA for text-mode output)...'
    & $vbox modifyvm $UUID --graphicscontroller VBoxVGA 2>&1 | Out-Null
    & $vbox modifyvm $UUID --vram 16                  2>&1 | Out-Null

} else {
    Log "Creating new VM '$VmName'..."
    $out = & $vbox createvm --name $VmName --ostype 'Other_64' --register 2>&1
    if ($LASTEXITCODE -ne 0) { Fail "createvm failed: $out" }

    $m2 = [regex]::Match(($out | Out-String), 'UUID:\s+([0-9a-f-]{36})')
    if ($m2.Success) { $UUID = $m2.Groups[1].Value }
    else { Fail 'Could not read UUID from createvm output.' }
    Log "Created VM  (UUID: $UUID)"

    VBox modifyvm  $UUID --memory 256 --cpus 1                               | Out-Null
    VBox modifyvm  $UUID --boot1 disk --boot2 none --boot3 none --boot4 none | Out-Null
    VBox modifyvm  $UUID --firmware efi                                       | Out-Null
    VBox modifyvm  $UUID --vram 16                                            | Out-Null
    # VBoxVGA = legacy VGA card, exposes 0xB8000 text buffer to the guest
    # vmsvga / vmsvga2 do NOT support legacy VGA text mode
    VBox modifyvm  $UUID --graphicscontroller VBoxVGA                         | Out-Null
    VBox modifyvm  $UUID --audio-driver none                                  | Out-Null
    VBox modifyvm  $UUID --usb off                                            | Out-Null
    VBox storagectl $UUID --name SATA --add sata --controller IntelAhci      | Out-Null
}

# Persist UUID
$UUID | Set-Content $UuidFile

# Unregister any previously registered medium at this path.
# mkdisk.py generates a new UUID on every build; without this VirtualBox
# rejects the new VHD because the UUID in the file no longer matches its registry.
Log "Unregistering old VHD from media registry (if any)..."
& $vbox closemedium disk $VhdPath 2>&1 | Out-Null   # ignore errors (first run has nothing to close)

# Attach freshly-built VHD
VBox storageattach $UUID --storagectl SATA --port 0 --device 0 `
     --type hdd --medium $VhdPath | Out-Null
Pass "VHD attached"

# ---- Step 4: Start VM -------------------------------------------------------
Step "Starting VM '$VmName'"
VBox startvm $UUID | Out-Null
Pass "VM window opened"

Write-Host ''
Write-Host '  +-----------------------------------------------------+' -ForegroundColor Cyan
Write-Host '  |  MicroNT is booting in the VirtualBox window.      |' -ForegroundColor Cyan
Write-Host '  |                                                     |' -ForegroundColor Cyan
Write-Host '  |  The VirtualBox splash fades, then you see:        |' -ForegroundColor Cyan
Write-Host '  |    MicroNT Microkernel  -  Build M22               |' -ForegroundColor Cyan
Write-Host '  |    [USER] MicroNT Shell                            |' -ForegroundColor Cyan
Write-Host '  |    [USER] > _                                       |' -ForegroundColor Cyan
Write-Host '  |                                                     |' -ForegroundColor Cyan
Write-Host '  |  Click the window and start typing.                |' -ForegroundColor Cyan
Write-Host '  |  Right Ctrl = release keyboard capture.            |' -ForegroundColor Cyan
Write-Host '  +-----------------------------------------------------+' -ForegroundColor Cyan
Write-Host ''
