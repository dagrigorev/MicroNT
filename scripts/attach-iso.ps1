#Requires -Version 5.1
<#
.SYNOPSIS
    Attach (or re-attach) MicroNT.iso to the VM DVD drive.
    Safe to run any time - detaches old medium first if one is loaded.
#>
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root     = Split-Path $PSScriptRoot -Parent
$UuidFile = Join-Path $Root 'artifacts\.vmuuid'
$IsoPath  = Join-Path $Root 'artifacts\MicroNT.iso'

$vbox = $null
if ($env:VBOXMANAGE_PATH) { $vbox = $env:VBOXMANAGE_PATH }
if (-not $vbox) {
    foreach ($c in @(
        'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe',
        'C:\Program Files (x86)\Oracle\VirtualBox\VBoxManage.exe'
    )) { if (Test-Path $c) { $vbox = $c; break } }
}
if (-not $vbox) { Write-Host '[ERROR] VBoxManage not found.' -ForegroundColor Red; exit 1 }

if (-not (Test-Path $UuidFile)) {
    Write-Host '[ERROR] No UUID file found. Run create-vbox-vm.ps1 first.' -ForegroundColor Red; exit 1
}
if (-not (Test-Path $IsoPath)) {
    Write-Host '[ERROR] MicroNT.iso not found. Run make-iso.ps1 first.' -ForegroundColor Red; exit 1
}

$UUID = (Get-Content $UuidFile -Raw).Trim()
Write-Host "[OK] VM UUID: $UUID"
Write-Host "[OK] ISO:     $IsoPath"

# Detach any existing medium first (ignore errors - drive may be empty)
& $vbox storageattach $UUID --storagectl IDE --port 0 --device 0 `
    --type dvddrive --medium emptydrive 2>$null | Out-Null

# Attach the new ISO
$out = & $vbox storageattach $UUID --storagectl IDE --port 0 --device 0 `
    --type dvddrive --medium $IsoPath 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] storageattach failed: $out" -ForegroundColor Red; exit 1
}

Write-Host '[SUCCESS] ISO attached.' -ForegroundColor Green
Write-Host '  Run: .\scripts\run-virtualbox.ps1'
