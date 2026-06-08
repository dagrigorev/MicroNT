#Requires -Version 5.1
<#
.SYNOPSIS
    Build MicroNT user-mode PE32+ images (EXEs/DLLs) from C++ source and embed
    them as kernel/ldr/*_pe.h blobs the kernel loader maps and runs.

    Uses the same clang/lld-link toolchain as the UEFI bootloader.
#>
param([switch]$Verbose)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root    = Split-Path $PSScriptRoot -Parent
$Build   = Join-Path $Root 'build\user'
$LdrDir  = Join-Path $Root 'kernel\ldr'
New-Item -ItemType Directory -Force $Build | Out-Null

function Need($name) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if (-not $c) { Write-Host "[ERROR] '$name' not found" -ForegroundColor Red; exit 1 }
    return $c.Source
}
$clangpp = Need 'clang++'
$lldlink = Need 'lld-link'
$python  = (Get-Command python -ErrorAction SilentlyContinue) ?? (Get-Command python3)

# Compile + link one freestanding user program into a PE32+, then embed it.
#   $src   : source .cpp (relative to repo root)
#   $name  : symbol/base name -> s_<name>_pe in kernel/ldr/<name>_pe.h
#   $sub   : PE subsystem (console / windows)
#   $entry : entry symbol
function Build-UserPe($src, $name, $sub, $entry) {
    $srcPath = Join-Path $Root $src
    $obj     = Join-Path $Build "$name.obj"
    $exe     = Join-Path $Build "$name.pe"
    $hdr     = Join-Path $LdrDir "${name}_pe.h"

    Write-Host "=== $name ($src) ===" -ForegroundColor Cyan
    $cflags = @(
        '--target=x86_64-unknown-windows'
        '-ffreestanding', '-fno-stack-protector', '-mno-red-zone'
        '-nostdlib', '-fno-exceptions', '-fno-rtti'
        '-std=c++20', '-Os', '-fno-builtin'
        '-c', $srcPath, '-o', $obj
    )
    & $clangpp @cflags
    if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] compile $name failed" -ForegroundColor Red; exit 1 }

    $lflags = @(
        '/nodefaultlib', "/subsystem:$sub", "/entry:$entry"
        '/fixed', '/align:4096', '/filealign:512'
        "/out:$exe", $obj
    )
    & $lldlink @lflags
    if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] link $name failed" -ForegroundColor Red; exit 1 }

    & $python.Source (Join-Path $Root 'tools\pe2h.py') $exe $name $hdr
    if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] embed $name failed" -ForegroundColor Red; exit 1 }
    Write-Host "[OK] $hdr" -ForegroundColor Green
}

Write-Host "`n=== MicroNT user-mode PE build ===" -ForegroundColor Cyan
Build-UserPe 'user\test\wintest.cpp' 'wintest' 'console' 'Entry'

Write-Host "`n[SUCCESS] user PE blobs generated." -ForegroundColor Green
