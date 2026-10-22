<#
.SYNOPSIS
    Builds and runs the host unit tests for the pure C++ modules of
    native/MultiTimer/src (TimeMath, TimerEngine, mini_json, BilibiliParse,
    ConfigStore, WifiStore, PageRenderer, LiveFormat).

.DESCRIPTION
    Compiles the real application sources directly (no copies) together with
    tests/test_*.cpp and runs the resulting executable.

    ConfigStore normally reads/writes /data/multitimer.conf (device path).
    The host build therefore defines MULTITIMER_HOST_CONFIG_DIR=tests/out/data
    so load()/save()/v1-migration can be exercised on a PC; the device build
    never defines it and keeps /data. The test process runs from the repository
    root so that relative path resolves inside tests/out.

    Toolchain order: MSVC cl.exe (PATH, else vswhere + vcvars64) ->
    g++ -> clang++. Objects/binaries go to tests/out, never to build/.

.PARAMETER Toolchain
    auto (default), msvc or gcc.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1
#>
[CmdletBinding()]
param(
    [ValidateSet('auto', 'msvc', 'gcc')]
    [string]$Toolchain = 'auto'
)

$ErrorActionPreference = 'Stop'

$testsDir = $PSScriptRoot
$repoRoot = Split-Path -Parent $testsDir
$srcDir = Join-Path $repoRoot 'native\MultiTimer\src'
$outDir = Join-Path $testsDir 'out'
# MSVC has no POSIX <unistd.h>; the config stores use fsync() when saving.
# This include dir is host-build only and never touches native/MultiTimer.
$shimDir = Join-Path $testsDir 'shim'

if (-not (Test-Path $srcDir)) {
    throw "Source directory not found: $srcDir"
}

$testSources = @(Get-ChildItem -Path $testsDir -Filter 'test_*.cpp' |
        Sort-Object Name | ForEach-Object { $_.FullName })
if ($testSources.Count -eq 0) {
    throw "No test_*.cpp files found in $testsDir"
}

$appSources = @(
    (Join-Path $srcDir 'timer\TimeMath.cpp'),
    (Join-Path $srcDir 'timer\TimerEngine.cpp'),
    (Join-Path $srcDir 'net\mini_json.cpp'),
    (Join-Path $srcDir 'net\BilibiliParse.cpp'),
    (Join-Path $srcDir 'config\ConfigStore.cpp'),
    (Join-Path $srcDir 'config\WifiStore.cpp'),
    (Join-Path $srcDir 'pages\PageRenderer.cpp'),
    (Join-Path $srcDir 'live\LiveFormat.cpp'),
    (Join-Path $srcDir 'utils\Surface.cpp')
)
foreach ($file in $appSources) {
    if (-not (Test-Path $file)) {
        throw "Missing application source: $file"
    }
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
# Host-only ConfigStore directory (see MULTITIMER_HOST_CONFIG_DIR below).
$configDataDir = Join-Path $outDir 'data'
New-Item -ItemType Directory -Force -Path $configDataDir | Out-Null
# Relative on purpose: test code and application code must resolve the same
# directory, and this path can never contain spaces.
$hostConfigDirArg = 'MULTITIMER_HOST_CONFIG_DIR=tests/out/data'

function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        return $null
    }
    $result = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $result) {
        return $null
    }
    return ($result | Select-Object -First 1).Trim()
}

function Import-MsvcEnvironment {
    param([string]$VcVarsPath)
    $lines = & cmd.exe /c "`"$VcVarsPath`" >nul 2>&1 && set"
    foreach ($line in $lines) {
        $eq = $line.IndexOf('=')
        if ($eq -gt 0) {
            Set-Item -Path ("env:" + $line.Substring(0, $eq)) -Value $line.Substring($eq + 1)
        }
    }
}

function Find-MsvcCompiler {
    param([string]$VsInstallPath)
    if (-not $VsInstallPath) {
        return $null
    }
    $toolsetRoot = Join-Path $VsInstallPath 'VC\Tools\MSVC'
    if (-not (Test-Path $toolsetRoot)) {
        return $null
    }
    $toolset = Get-ChildItem -Path $toolsetRoot -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } |
        Select-Object -Last 1
    if (-not $toolset) {
        return $null
    }
    $candidate = Join-Path $toolset.FullName 'bin\Hostx64\x64\cl.exe'
    if (Test-Path $candidate) {
        return $candidate
    }
    return $null
}

$exePath = Join-Path $outDir 'multitimer_tests.exe'
$built = $false

if ($Toolchain -eq 'auto' -or $Toolchain -eq 'msvc') {
    $clPath = $null
    $clCommand = Get-Command cl.exe -ErrorAction SilentlyContinue
    if ($clCommand) {
        $clPath = $clCommand.Source
    }
    if (-not $clPath) {
        $vsPath = Get-VsInstallPath
        if ($vsPath) {
            Write-Host "[toolchain] Visual Studio: $vsPath"
            $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $vcvars) {
                Import-MsvcEnvironment -VcVarsPath $vcvars
                $clCommand = Get-Command cl.exe -ErrorAction SilentlyContinue
                if ($clCommand) {
                    $clPath = $clCommand.Source
                }
            }
            if (-not $clPath) {
                $clPath = Find-MsvcCompiler -VsInstallPath $vsPath
            }
        }
    }

    if ($clPath) {
        $env:DISPLAY = ''
        Write-Host "[toolchain] MSVC cl.exe: $clPath"
        if ($env:VCToolsVersion) {
            Write-Host "[toolchain] VCToolsVersion: $env:VCToolsVersion"
        }
        $clArgs = @(
            '/nologo',
            '/std:c++14',
            '/EHsc',
            '/W4',
            '/wd4127',
            '/wd4996',
            '/utf-8',
            '/D_CRT_SECURE_NO_WARNINGS',
            '/DPROGMEM=',
            ('/D' + $hostConfigDirArg),
            '/I', $shimDir,
            '/I', $srcDir,
            ('/Fo' + $outDir + '\'),
            ('/Fe' + $exePath)
        )
        Write-Host "[build] cl /std:c++14 /EHsc /W4 /I native\MultiTimer\src ..."
        & $clPath @clArgs @testSources @appSources
        if ($LASTEXITCODE -ne 0) {
            throw "MSVC compilation failed (exit code $LASTEXITCODE)"
        }
        $built = $true
    }
    elseif ($Toolchain -eq 'msvc') {
        throw 'MSVC was requested but cl.exe could not be located.'
    }
}

if (-not $built) {
    $gxx = Get-Command g++.exe -ErrorAction SilentlyContinue
    if (-not $gxx) {
        $gxx = Get-Command clang++.exe -ErrorAction SilentlyContinue
    }
    if (-not $gxx) {
        throw 'No host C++ compiler found (cl.exe, g++.exe and clang++.exe are all missing).'
    }
    Write-Host "[toolchain] $($gxx.Source)"
    $gccArgs = @('-std=c++11', '-Wall', '-Wextra', '-O1', '-I', $srcDir,
        ('-D' + $hostConfigDirArg), '-o', $exePath)
    & $gxx.Source @gccArgs @testSources @appSources
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed (exit code $LASTEXITCODE)"
    }
}

Write-Host ''
Write-Host "[run] $exePath"
# Run from the repository root so the relative host config dir
# (tests/out/data) resolves the same way for tests and application code.
Push-Location $repoRoot
try {
    & $exePath
    $exitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}
Write-Host ''
Write-Host "[run] exit code: $exitCode (0 = all cases passed)"
exit $exitCode
