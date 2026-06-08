#Requires -Version 5.1
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$VM_NAME   = 'MicroNT-Test'
$RAM_MB    = 512
$Root      = Split-Path $PSScriptRoot -Parent
$VhdPath   = Join-Path $Root 'artifacts\MicroNT.vhd'
$SerialLog = Join-Path $Root 'artifacts\serial.log'
$UuidFile  = Join-Path $Root 'artifacts\.vmuuid'

# Locate VBoxManage
$vbox = $null
if ($env:VBOXMANAGE_PATH) { $vbox = $env:VBOXMANAGE_PATH }
if (-not $vbox) {
    foreach ($c in @(
        'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe',
        'C:\Program Files (x86)\Oracle\VirtualBox\VBoxManage.exe'
    )) { if (Test-Path $c) { $vbox = $c; break } }
}
if (-not $vbox) {
    Write-Host '[ERROR] VBoxManage not found.' -ForegroundColor Red; exit 1
}
Write-Host "[OK] VBoxManage: $vbox" -ForegroundColor Green

function Invoke-VBox {
    $out = & $vbox @args 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[VBOX] $out" -ForegroundColor Red
        throw "VBoxManage failed (exit $LASTEXITCODE)"
    }
    return $out
}

function Extract-UUID($text) {
    $m = [regex]::Match(($text | Out-String), 'UUID:\s+([0-9a-f-]{36})')
    if ($m.Success) { return $m.Groups[1].Value }
    return $null
}

# Already configured by this script?
if (Test-Path $UuidFile) {
    $saved = (Get-Content $UuidFile -Raw).Trim()
    Write-Host "[WARN] VM already configured. UUID: $saved" -ForegroundColor Yellow
    Write-Host '       To recreate:'
    Write-Host "         VBoxManage unregistervm $saved --delete"
    Write-Host "         Remove-Item '$UuidFile'"
    exit 0
}

# .vbox file path (VirtualBox default location)
$VboxFile = "$env:USERPROFILE\VirtualBox VMs\$VM_NAME\$VM_NAME.vbox"

$UUID = $null

# Case 1: .vbox file exists but is not registered - register it
if (Test-Path $VboxFile) {
    Write-Host "[INFO] Found existing .vbox file, registering it..." -ForegroundColor Cyan
    $regOut = & $vbox registervm $VboxFile 2>&1
    Write-Host $regOut
    if ($LASTEXITCODE -ne 0) {
        # Still failing - delete the orphaned file and recreate
        Write-Host '[INFO] Registration failed. Removing orphaned files and recreating...' -ForegroundColor Yellow
        $vboxDir = "$env:USERPROFILE\VirtualBox VMs\$VM_NAME"
        Remove-Item -Recurse -Force $vboxDir -ErrorAction SilentlyContinue
        Write-Host "[INFO] Removed: $vboxDir"
    } else {
        $UUID = Extract-UUID $regOut
        if (-not $UUID) {
            # registervm succeeded but no UUID in output - get it from showvminfo
            $infoOut = & $vbox showvminfo $VM_NAME --machinereadable 2>&1
            $m2 = [regex]::Match(($infoOut | Out-String), 'UUID="([0-9a-f-]{36})"')
            if ($m2.Success) { $UUID = $m2.Groups[1].Value }
        }
        Write-Host "[OK] Registered existing VM. UUID: $UUID" -ForegroundColor Green
    }
}

# Case 2: need to create fresh
if (-not $UUID) {
    Write-Host "Creating VM '$VM_NAME'..."
    $createOut = & $vbox createvm --name $VM_NAME --ostype 'Other_64' --register 2>&1
    Write-Host $createOut
    if ($LASTEXITCODE -ne 0) {
        Write-Host '[ERROR] createvm failed.' -ForegroundColor Red; exit 1
    }
    $UUID = Extract-UUID $createOut
    if (-not $UUID) {
        Write-Host '[ERROR] Could not extract UUID from createvm output.' -ForegroundColor Red; exit 1
    }
    Write-Host "[OK] Created VM. UUID: $UUID" -ForegroundColor Green
}

# Save UUID
New-Item -ItemType Directory -Force (Split-Path $UuidFile) | Out-Null
[System.IO.File]::WriteAllText($UuidFile, $UUID)

# Configure by UUID
Invoke-VBox modifyvm $UUID --memory $RAM_MB --cpus 1 | Out-Null
Invoke-VBox modifyvm $UUID --boot1 disk --boot2 none --boot3 none --boot4 none | Out-Null
Invoke-VBox modifyvm $UUID --firmware efi | Out-Null
Invoke-VBox modifyvm $UUID --vram 16 | Out-Null
Invoke-VBox modifyvm $UUID --audio-driver none | Out-Null
Invoke-VBox modifyvm $UUID --usb off | Out-Null
Invoke-VBox modifyvm $UUID --graphicscontroller vmsvga | Out-Null

# Auto-fit: the bootloader picks the largest GOP mode, and VirtualBox EFI
# exposes the mode named here. Change this to the size you want the desktop
# to fill (e.g. 1600x900, 1280x720). Live resize-on-drag needs a guest GPU
# driver, which MicroNT does not have yet.
Invoke-VBox setextradata $UUID 'VBoxInternal2/EfiGraphicsResolution' '1920x1080' | Out-Null

# Serial COM1 to file
New-Item -ItemType Directory -Force (Split-Path $SerialLog) | Out-Null
Invoke-VBox modifyvm $UUID --uart1 0x3F8 4 | Out-Null
Invoke-VBox modifyvm $UUID --uartmode1 file $SerialLog | Out-Null

# DVD with ISO - add IDE controller only if it doesn't exist yet
$ctrlOut = & $vbox showvminfo $UUID 2>&1 | Out-String
if ($ctrlOut -notmatch 'IDE') {
    Invoke-VBox storagectl $UUID --name 'SATA' --add sata --controller IntelAhci | Out-Null
}
if (Test-Path $VhdPath) {
    Invoke-VBox storageattach $UUID --storagectl 'IDE' --port 0 --device 0 `
        --type hdd --medium $VhdPath | Out-Null
    Write-Host "[OK] ISO attached: $VhdPath" -ForegroundColor Green
} else {
    Write-Host '[WARN] ISO not found - run make-iso.ps1 first' -ForegroundColor Yellow
}

Write-Host "[SUCCESS] VM '$VM_NAME' ready. UUID: $UUID" -ForegroundColor Green
Write-Host '  Run: .\scripts\run-virtualbox.ps1'
