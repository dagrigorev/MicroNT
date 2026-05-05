#Requires -Version 5.1
<#
.SYNOPSIS
    MicroNT full automation: build, ISO, VM setup, boot, and verify serial output.

.DESCRIPTION
    Runs the complete workflow end-to-end:
      1. Build kernel ELF + UEFI bootloader
      2. Package FAT12 UEFI-bootable ISO
      3. Destroy and recreate the VM cleanly
      4. Boot the VM
      5. Wait for kernel serial output
      6. Verify expected boot lines appeared
      7. Power off VM
      Report PASS or FAIL with details.

.PARAMETER BootTimeout
    Seconds to wait for kernel boot output. Default 60.

.PARAMETER SkipBuild
    Skip build step (use existing artifacts).

.PARAMETER SkipVmRecreate
    Skip VM deletion and recreation (reuse existing VM).
#>
param(
    [int]   $BootTimeout    = 120,
    [switch]$SkipBuild      = $false,
    [switch]$SkipVmRecreate = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root      = Split-Path $PSScriptRoot -Parent
$Artifacts = Join-Path $Root 'artifacts'
$UuidFile  = Join-Path $Artifacts '.vmuuid'
$SerialLog = Join-Path $Artifacts 'serial.log'
$VM_NAME   = 'MicroNT-Test'

$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python) { $Python = Get-Command python3 -ErrorAction SilentlyContinue }

$vbox = $null
foreach ($c in @(
    'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe',
    'C:\Program Files (x86)\Oracle\VirtualBox\VBoxManage.exe'
)) { if (Test-Path $c) { $vbox = $c; break } }

# Expected serial lines (all must appear for PASS)
$ExpectedLines = @(
    '[MicroNT] Boot started',
    '[MicroNT] GDT initialized',
    '[MicroNT] IDT initialized',
    '[MicroNT] HAL initialized',
    '[MicroNT] PIT initialized',
    '[MicroNT] Physical memory manager initialized',
    '[MicroNT] Virtual memory manager initialized',
    '[MicroNT] M3 ready',
    '[MicroNT] Object manager initialized',
    '[MicroNT] M4 ready',
    '[MicroNT] Process manager initialized',
    '[MicroNT] M5 ready',
    '[MicroNT] M6 ready',
    '[MicroNT] M7 ready',
    '[MicroNT] M8 ready',
    '[MicroNT] M9 ready',
    '[MicroNT] PE loader initialized',
    '[MicroNT] M2 ready',
    '[MicroNT] Ready'
)

$PASS = $true
$Results = [System.Collections.Generic.List[string]]::new()

function Log($msg, $color='Cyan') {
    Write-Host $msg -ForegroundColor $color
}

function Fail($msg) {
    script:Log "[FAIL] $msg" Red
    $script:PASS = $false
    $script:Results.Add("FAIL: $msg")
}

function Pass($msg) {
    script:Log "[PASS] $msg" Green
    $script:Results.Add("PASS: $msg")
}

function Step($name) {
    Write-Host "`n--- $name ---" -ForegroundColor Yellow
}

function VBox {
    $out = & $vbox @args 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "VBoxManage failed: $args`n$out"
    }
    return $out
}

function VBoxSilent {
    & $vbox @args 2>&1 | Out-Null
    # intentionally ignore exit code
    $global:LASTEXITCODE = 0
}

# ============================================================
# Pre-flight checks
# ============================================================
Step 'Pre-flight checks'

if (-not $Python) {
    Fail 'Python not found'; exit 1
}
Log "[OK] Python: $($Python.Source)"

if (-not $vbox) {
    Fail 'VBoxManage not found'; exit 1
}
Log "[OK] VBoxManage: $vbox"

New-Item -ItemType Directory -Force $Artifacts | Out-Null

# ============================================================
# Step 1: Build
# ============================================================
Step 'Build kernel + bootloader'

if ($SkipBuild) {
    Log '  Skipping build (SkipBuild set)' Yellow
} else {
    & (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0) { Fail 'build.ps1 failed'; exit 1 }
    Pass 'Kernel + bootloader built'
}

# ============================================================
# Step 2: Package ISO
# ============================================================
Step 'Package ISO'

# Clear old serial log
if (Test-Path $SerialLog) { Remove-Item $SerialLog -Force }

$VhdPath = Join-Path $Artifacts 'MicroNT.vhd'

# ============================================================
# Step 3: VM setup  (destroy old VM BEFORE building VHD)
# ============================================================
Step 'VM setup'

if ($SkipVmRecreate) {
    Log '  Skipping VM recreation (SkipVmRecreate set)' Yellow
    if (-not (Test-Path $UuidFile)) {
        Fail 'No UUID file and SkipVmRecreate set - cannot continue'
        exit 1
    }
    $UUID = (Get-Content $UuidFile -Raw).Trim()
    # Verify UUID is still registered
    $checkVm = & $vbox showvminfo $UUID 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        Log "  [WARN] UUID $UUID no longer registered - clearing stale UUID file" Yellow
        Remove-Item $UuidFile -Force -ErrorAction SilentlyContinue
        Fail "VM was removed externally. Re-run without -SkipVmRecreate to recreate it."
        exit 1
    }
    Log "  Using existing UUID: $UUID"
} else {
    # -- Helper: silently destroy a VM by UUID (ignores all errors) -
    function Destroy-VM($id) {
        $prev = $ErrorActionPreference
        $ErrorActionPreference = 'SilentlyContinue'
        & $vbox controlvm $id poweroff 2>&1 | Out-Null
        Start-Sleep -Seconds 2
        # Do NOT use --delete: it removes attached media files (the VHD).
        & $vbox unregistervm $id 2>&1 | Out-Null
        $ErrorActionPreference = $prev
    }

    # -- Clean up stale .vmuuid (UUID may have been deleted from VirtualBox UI) --
    if (Test-Path $UuidFile) {
        $oldUuid = (Get-Content $UuidFile -Raw).Trim()
        # Check if UUID is still registered in VirtualBox
        $prev = $ErrorActionPreference; $ErrorActionPreference = 'SilentlyContinue'
        $vmInfo = & $vbox showvminfo $oldUuid 2>&1 | Out-String
        $stillExists = ($LASTEXITCODE -eq 0)
        $ErrorActionPreference = $prev

        if ($stillExists) {
            Log "  Destroying old VM $oldUuid ..."
            Destroy-VM $oldUuid
        } else {
            Log "  [INFO] UUID $oldUuid not in VirtualBox (deleted externally) - skipping unregister" Yellow
        }
        Remove-Item $UuidFile -Force -ErrorAction SilentlyContinue
    }

    # -- Clean up any VM still registered under our name ---------
    $byName = & $vbox list vms 2>$null | Select-String $VM_NAME
    if ($byName) {
        $nameMatch = [regex]::Match(($byName | Out-String), '\{([0-9a-f-]{36})\}')
        if ($nameMatch.Success) {
            $nUuid = $nameMatch.Groups[1].Value
            Log "  Destroying VM by name (UUID: $nUuid) ..."
            Destroy-VM $nUuid
        }
    }

    # -- Remove orphaned .vbox directory (left by VirtualBox UI delete) --
    $vboxDir = "$env:USERPROFILE\VirtualBox VMs\$VM_NAME"
    if (Test-Path $vboxDir) {
        Log "  Removing orphaned .vbox directory: $vboxDir" Yellow
        Remove-Item $vboxDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 1

    # -- Create VM ------------------------------------------------
    Log '  Creating VM ...'
    $createOut = & $vbox createvm --name $VM_NAME --ostype 'Other_64' --register 2>&1
    if ($LASTEXITCODE -ne 0) { Fail "createvm failed: $createOut"; exit 1 }

    $uuidMatch = [regex]::Match(($createOut | Out-String), 'UUID:\s+([0-9a-f-]{36})')
    if (-not $uuidMatch.Success) { Fail 'Could not extract UUID'; exit 1 }
    $UUID = $uuidMatch.Groups[1].Value
    [System.IO.File]::WriteAllText($UuidFile, $UUID)
    Log "  UUID: $UUID"

    # Configure
    VBox modifyvm $UUID --memory 512 --cpus 1 | Out-Null
    VBox modifyvm $UUID --boot1 disk --boot2 none --boot3 none --boot4 none | Out-Null
    VBox modifyvm $UUID --firmware efi | Out-Null
    VBox modifyvm $UUID --vram 16 | Out-Null
    VBox modifyvm $UUID --audio-driver none | Out-Null
    VBox modifyvm $UUID --usb off | Out-Null
    VBox modifyvm $UUID --graphicscontroller vmsvga | Out-Null
    VBox modifyvm $UUID --uart1 0x3F8 4 | Out-Null
    VBox modifyvm $UUID --uartmode1 file $SerialLog | Out-Null
    VBox storagectl $UUID --name SATA --add sata --controller IntelAhci | Out-Null
    # VHD attached after it is built (see 'Build VHD disk image' step below)

    Pass "VM created (UUID: $UUID)"
}

# Build VHD now (after any old VM/media destruction)
Step 'Build VHD disk image'
& $Python.Source (Join-Path $Root 'tools\mkdisk.py') `
    (Join-Path $Artifacts 'BOOTX64.EFI') `
    (Join-Path $Artifacts 'micront.elf') `
    $VhdPath

if ($LASTEXITCODE -ne 0 -or -not (Test-Path $VhdPath)) {
    Fail 'mkdisk.py failed'; exit 1
}
$vhdMB = [math]::Round((Get-Item $VhdPath).Length / 1MB, 2)
if ($vhdMB -lt 60) { Fail "VHD too small ($vhdMB MB)"; exit 1 }
Pass "VHD built: $vhdMB MB"

# Attach VHD
Log '  Attaching VHD ...'
$prev2 = $ErrorActionPreference; $ErrorActionPreference = 'SilentlyContinue'
& $vbox storageattach $UUID --storagectl SATA --port 0 --device 0 --type hdd --medium none 2>&1 | Out-Null
$ErrorActionPreference = $prev2
VBox storageattach $UUID --storagectl SATA --port 0 --device 0 `
    --type hdd --medium $VhdPath | Out-Null
Pass 'VHD attached'

# ============================================================
# Step 4: Boot VM
# ============================================================
Step 'Boot VM'

# Remove old serial log
if (Test-Path $SerialLog) { Remove-Item $SerialLog -Force }

VBox startvm $UUID --type headless | Out-Null
Pass 'VM started (headless)'

# ============================================================
# Step 5: Wait for serial output and verify
# ============================================================
Step "Wait for boot output (timeout: ${BootTimeout}s)"

$deadline  = (Get-Date).AddSeconds($BootTimeout)
$found     = @{}
$gotReady  = $false
$lastSize  = 0

while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500

    if (-not (Test-Path $SerialLog)) { continue }

    $size = (Get-Item $SerialLog).Length
    if ($size -eq $lastSize)  { continue }
    $lastSize = $size

    $content = Get-Content $SerialLog -Raw -ErrorAction SilentlyContinue
    if (-not $content) { continue }

    foreach ($line in $ExpectedLines) {
        if (-not $found.ContainsKey($line) -and $content.Contains($line)) {
            $found[$line] = $true
            Log "  [GOT] $line" Green
        }
    }

    if ($found.ContainsKey('[MicroNT] Ready')) {
        $gotReady = $true
        break
    }

    # Detect panic / EFI failure
    if ($content -match 'KERNEL PANIC|BdsDxe: No bootable') {
        Log '  [ERROR] Detected failure in serial output' Red
        break
    }
}

# ============================================================
# Step 6: Results
# ============================================================
Step 'Results'

$missing = @($ExpectedLines | Where-Object { -not $found.ContainsKey($_) })

if ($gotReady -and $missing.Count -eq 0) {
    Pass 'All expected boot lines received'
} else {
    if (-not $gotReady) {
        Fail "[MicroNT] Ready was NOT received within ${BootTimeout}s"
    }
    foreach ($m in $missing) {
        Fail "Missing: $m"
    }
}

# Print serial log tail
if (Test-Path $SerialLog) {
    Write-Host "`n--- Serial output ---" -ForegroundColor Yellow
    Get-Content $SerialLog | Select-Object -Last 30 |
        ForEach-Object { Write-Host "  $_" }
}

# ============================================================
# Step 7: Power off VM
# ============================================================
Step 'Power off VM'
$prevPO = $ErrorActionPreference; $ErrorActionPreference = 'SilentlyContinue'
& $vbox controlvm $UUID poweroff 2>&1 | Out-Null
$ErrorActionPreference = $prevPO
Log '  VM powered off.' Green

# ============================================================
# Final summary
# ============================================================
Write-Host ''
Write-Host ('=' * 50) -ForegroundColor White
if ($PASS) {
    Write-Host '  RESULT: PASS' -ForegroundColor Green
} else {
    Write-Host '  RESULT: FAIL' -ForegroundColor Red
}
Write-Host ('=' * 50) -ForegroundColor White
$Results | ForEach-Object {
    $col = if ($_ -match '^PASS') { 'Green' } else { 'Red' }
    Write-Host "  $_" -ForegroundColor $col
}

exit $(if ($PASS) { 0 } else { 1 })
