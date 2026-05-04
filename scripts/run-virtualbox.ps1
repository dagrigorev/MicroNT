#Requires -Version 5.1
<#
.SYNOPSIS
    Start the MicroNT-Test VM. Uses UUID from artifacts/.vmuuid (immune to
    VirtualBox name-lookup bugs across VBoxSVC restarts).
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root     = Split-Path $PSScriptRoot -Parent
$UuidFile = Join-Path $Root 'artifacts\.vmuuid'

$vbox = if ($env:VBOXMANAGE_PATH) { $env:VBOXMANAGE_PATH } else {
    @(
        'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe',
        'C:\Program Files (x86)\Oracle\VirtualBox\VBoxManage.exe'
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $vbox) {
    Write-Host "[ERROR] VBoxManage not found." -ForegroundColor Red; exit 1
}

# ?? Resolve VM identifier ?????????????????????????????????????
$vmId = $null

if (Test-Path $UuidFile) {
    $vmId = (Get-Content $UuidFile -Raw).Trim()
    Write-Host "[OK] Using UUID: $vmId" -ForegroundColor Green
} else {
    # Fallback: try by name
    Write-Host "[WARN] No UUID file found, trying by name 'MicroNT-Test'" -ForegroundColor Yellow
    $vmId = 'MicroNT-Test'
}

# ?? Start VM ?????????????????????????????????????????????????
Write-Host "Starting VM $vmId ..."
$out = & $vbox startvm $vmId --type gui 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] startvm failed:" -ForegroundColor Red
    Write-Host $out
    Write-Host ""
    Write-Host "Registered VMs:" -ForegroundColor Yellow
    & $vbox list vms 2>$null | ForEach-Object { Write-Host "  $_" }
    Write-Host ""
    Write-Host "If the list is empty, delete and recreate:" -ForegroundColor Cyan
    Write-Host "  Remove-Item '$UuidFile' -ErrorAction SilentlyContinue"
    Write-Host "  .\scripts\create-vbox-vm.ps1"
    exit 1
}

Write-Host $out
Write-Host "[OK] VM started." -ForegroundColor Green
Write-Host "     Serial log:  .\artifacts\serial.log"
Write-Host "     Watch live:  .\scripts\debug-serial.ps1"
