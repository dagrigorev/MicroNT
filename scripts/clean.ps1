#Requires -Version 5.1
<# MicroNT - remove generated build artifacts #>

$Root = Split-Path $PSScriptRoot -Parent

foreach ($dir in @('build', 'artifacts')) {
    $path = Join-Path $Root $dir
    if (Test-Path $path) {
        Remove-Item -Recurse -Force $path
        Write-Host "Removed: $path" -ForegroundColor Yellow
    }
}
Write-Host "Clean complete." -ForegroundColor Green
