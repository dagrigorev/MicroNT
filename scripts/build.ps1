#Requires -Version 5.1
<#
.SYNOPSIS
    MicroNT build script - builds kernel ELF and UEFI bootloader.
#>
param([switch]$Clean, [switch]$Verbose)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Find-Tool {
    param([string]$Name, [string]$EnvOverride)
    if ($EnvOverride -and (Test-Path $EnvOverride)) { return $EnvOverride }
    $f = Get-Command $Name -ErrorAction SilentlyContinue
    if ($f) { return $f.Source }
    return $null
}

function Require-Tool {
    param([string]$Name, [string]$EnvOverride, [string]$Hint)
    $p = Find-Tool -Name $Name -EnvOverride $EnvOverride
    if (-not $p) {
        Write-Host "[ERROR] '$Name' not found.  $Hint" -ForegroundColor Red; exit 1
    }
    Write-Host "[OK]    $Name -> $p" -ForegroundColor Green
    return $p
}

Write-Host "`n=== MicroNT Build ===" -ForegroundColor Cyan

$cmake    = Require-Tool 'cmake'   $env:CMAKE_PATH   "scoop install cmake"
$ninja    = Require-Tool 'ninja'   $null             "scoop install ninja"
$nasm     = Require-Tool 'nasm'    $env:NASM_PATH    "scoop install nasm"
$clang    = Require-Tool 'clang'   $null             "Install LLVM"
$clangpp  = Require-Tool 'clang++' $null             "Install LLVM"
$lldlink  = Require-Tool 'lld-link' $null            "Install LLVM (includes lld-link)"
Write-Host ""

$Root      = Split-Path $PSScriptRoot -Parent
$Build     = Join-Path $Root 'build'
$Artifacts = Join-Path $Root 'artifacts'

if ($Clean -and (Test-Path $Build)) {
    Remove-Item -Recurse -Force $Build
    Write-Host "Cleaned build/" -ForegroundColor Yellow
}
New-Item -ItemType Directory -Force $Build    | Out-Null
New-Item -ItemType Directory -Force $Artifacts | Out-Null

# ============================================================
# A. Build kernel (CMake + Ninja + cross-Clang + ld.lld)
# ============================================================
Write-Host "=== Kernel ===" -ForegroundColor Cyan

$KernelSrc    = Join-Path $Root 'kernel'
$KernelBuild  = Join-Path $Build 'kernel'
$ToolchainFile = Join-Path $Root 'cmake\Toolchain-kernel.cmake'

$configArgs = @('-S', $KernelSrc, '-B', $KernelBuild, '-G', 'Ninja',
                "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile",
                '-DCMAKE_BUILD_TYPE=Debug')
if ($Verbose) { $configArgs += '-DCMAKE_VERBOSE_MAKEFILE=ON' }

& $cmake @configArgs
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] Kernel CMake configure failed" -ForegroundColor Red; exit 1 }

$buildArgs = @('--build', $KernelBuild)
if ($Verbose) { $buildArgs += '--verbose' }

& $cmake @buildArgs
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] Kernel build failed" -ForegroundColor Red; exit 1 }

# Copy kernel ELF to artifacts
Copy-Item (Join-Path $KernelBuild 'micront.elf') $Artifacts -Force
Write-Host "[OK] Kernel: $Artifacts\micront.elf" -ForegroundColor Green

# ============================================================
# B. Build UEFI bootloader (clang++ PE32+ ? lld-link EFI)
# ============================================================
Write-Host "`n=== UEFI Bootloader ===" -ForegroundColor Cyan

$UefiBuild = Join-Path $Build 'uefi'
New-Item -ItemType Directory -Force $UefiBuild | Out-Null

$UefiSrc = Join-Path $Root 'boot\uefi\bootloader.cpp'
$UefiObj = Join-Path $UefiBuild 'bootloader.obj'
$UefiEfi = Join-Path $Artifacts 'BOOTX64.EFI'

$compileFlags = @(
    '--target=x86_64-unknown-windows'
    '-ffreestanding', '-fno-stack-protector', '-fshort-wchar'
    '-mno-red-zone', '-nostdlib', '-fno-exceptions', '-fno-rtti'
    '-std=c++20', '-O2', '-g'
    "-I$Root\boot\uefi"
    "-I$Root\kernel\include"
    '-c', $UefiSrc, '-o', $UefiObj
)

Write-Host "Compiling bootloader..."
& $clangpp @compileFlags
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] Bootloader compile failed" -ForegroundColor Red; exit 1 }

$linkFlags = @(
    '/nodefaultlib'
    '/subsystem:efi_application'
    '/entry:EfiMain'
    "/out:$UefiEfi"
    $UefiObj
)

Write-Host "Linking BOOTX64.EFI..."
& $lldlink @linkFlags
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] Bootloader link failed" -ForegroundColor Red; exit 1 }

Write-Host "[OK] Bootloader: $UefiEfi" -ForegroundColor Green
Write-Host "`n[SUCCESS] Build complete." -ForegroundColor Green
Write-Host "  Kernel:     $Artifacts\micront.elf"
Write-Host "  Bootloader: $Artifacts\BOOTX64.EFI"
Write-Host "  Next:       .\scripts\make-iso.ps1"
