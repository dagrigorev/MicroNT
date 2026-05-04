#Requires -Version 5.1
<# Tail the MicroNT serial log in real-time #>
$Root = Split-Path $PSScriptRoot -Parent
$Log  = Join-Path $Root 'artifacts\serial.log'
if (-not (Test-Path $Log)) {
    Write-Host "Serial log not found: $Log" -ForegroundColor Yellow
    Write-Host "Boot the VM first: .\scripts\run-virtualbox.ps1"
    exit 1
}
Write-Host "Tailing serial log (Ctrl+C to stop)..." -ForegroundColor Cyan
Get-Content $Log -Wait
