#requires -Version 5.1
<#
.SYNOPSIS
    在 Windows 上使用 FlyThings IDE 的 headless build 编译 MultiTimer。

.DESCRIPTION
    - 将 native/MultiTimer 复制到独立 workspace，避免污染源码目录。
    - 调用 FlyThings IDE (Eclipse CDT headlessbuild) 导入并 cleanBuild。
    - 首次构建时 -cleanBuild 可能因 Release/makefile 尚不存在而报 clean 失败，
      此时 IDE 仍会继续 build；脚本以 libzkgui.so 是否生成作为最终判定。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\tools\build.ps1

.EXAMPLE
    .\tools\build.ps1 -Configuration Release

.EXAMPLE
    .\tools\build.ps1 -Workspace build\my-workspace -LogPath build\my-build.log
#>
param(
    [string]$ProjectDir = "native/MultiTimer",
    [string]$Workspace = "build/ide-workspace",
    [string]$Configuration = "Release",
    [string]$LogPath = "build/headless-build.log"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$IdeExe = "D:\zkswe\FlyThingsIDE\bin\ide.exe"
$LogAbs = $null

function Resolve-RepoPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Base
    )
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Quote-NativeArg {
    param([Parameter(Mandatory = $true)][string]$Value)
    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

try {
    $ProjectAbs = Resolve-RepoPath -Path $ProjectDir -Base $RepoRoot
    $WorkspaceAbs = Resolve-RepoPath -Path $Workspace -Base $RepoRoot
    $LogAbs = Resolve-RepoPath -Path $LogPath -Base $RepoRoot

    if (-not (Test-Path -LiteralPath $IdeExe -PathType Leaf)) {
        throw "未找到 FlyThings IDE：$IdeExe"
    }
    if (-not (Test-Path -LiteralPath $ProjectAbs -PathType Container)) {
        throw "未找到工程目录：$ProjectAbs"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectAbs ".project") -PathType Leaf)) {
        throw "工程目录缺少 .project：$ProjectAbs"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectAbs "Manifest.xml") -PathType Leaf)) {
        throw "工程目录缺少 Manifest.xml：$ProjectAbs"
    }
    if ([string]::IsNullOrWhiteSpace($Configuration)) {
        throw "-Configuration 不能为空"
    }

    # 工程名优先取 .project 中的 <name>，同时作为 workspace 子目录名和构建配置前缀。
    $ProjectName = Split-Path -Leaf $ProjectAbs
    try {
        [xml]$ProjectXml = Get-Content -LiteralPath (Join-Path $ProjectAbs ".project") -Raw -Encoding UTF8
        $XmlName = [string]$ProjectXml.projectDescription.name
        if (-not [string]::IsNullOrWhiteSpace($XmlName)) {
            $ProjectName = $XmlName.Trim()
        }
    } catch {
        Write-Warning "读取 .project 工程名失败，使用目录名：$ProjectName"
    }

    if (-not (Test-Path -LiteralPath $WorkspaceAbs -PathType Container)) {
        New-Item -ItemType Directory -Path $WorkspaceAbs -Force | Out-Null
    } else {
        # Eclipse 的 workspace 一旦 import 过同名工程，删掉工程目录再 import 会失败；
        # 这里每次整目录重建。仅允许自动删除仓库 build/ 下的 workspace，避免误删用户数据。
        $RepoBuildPrefix = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "build"))
        $RepoBuildPrefix = $RepoBuildPrefix.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
        $WorkspaceFull = [System.IO.Path]::GetFullPath($WorkspaceAbs)
        if (-not $WorkspaceFull.StartsWith($RepoBuildPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "workspace 已存在且不在仓库 build/ 目录下，拒绝自动删除：$WorkspaceAbs"
        }
        Write-Host "Removing old workspace: $WorkspaceAbs"
        Remove-Item -LiteralPath $WorkspaceAbs -Recurse -Force
        New-Item -ItemType Directory -Path $WorkspaceAbs -Force | Out-Null
    }

    $ProjectDest = Join-Path $WorkspaceAbs $ProjectName
    $ProjectDest = [System.IO.Path]::GetFullPath($ProjectDest)
    $WorkspacePrefix = $WorkspaceAbs.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $ProjectDest.StartsWith($WorkspacePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "拒绝操作 workspace 之外的路径：$ProjectDest"
    }

    $BuildDir = Join-Path $RepoRoot "build"
    if (-not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }
    $LogDir = Split-Path -Parent $LogAbs
    if ([string]::IsNullOrWhiteSpace($LogDir)) {
        throw "-LogPath 必须包含文件名：$LogPath"
    }
    if (-not (Test-Path -LiteralPath $LogDir -PathType Container)) {
        New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    }
    # 临时输出文件与日志同名，避免并行构建时互相抢同一个 tmp 文件。
    $StdoutPath = "$LogAbs.stdout.tmp"
    $StderrPath = "$LogAbs.stderr.tmp"
    foreach ($tmp in @($LogAbs, $StdoutPath, $StderrPath)) {
        if (Test-Path -LiteralPath $tmp) {
            Remove-Item -LiteralPath $tmp -Force
        }
    }

    Write-Host "== MultiTimer headless build =="
    Write-Host "IDE       : $IdeExe"
    Write-Host "Project   : $ProjectAbs"
    Write-Host "Workspace : $WorkspaceAbs"
    Write-Host "Config    : $ProjectName/$Configuration"
    Write-Host "Log       : $LogAbs"

    # 每次都从干净副本构建，确保判定依据的 .so 不是上一次的残留。
    if (Test-Path -LiteralPath $ProjectDest) {
        Write-Host "Removing old project copy: $ProjectDest"
        Remove-Item -LiteralPath $ProjectDest -Recurse -Force
    }
    New-Item -ItemType Directory -Path $ProjectDest -Force | Out-Null
    Get-ChildItem -LiteralPath $ProjectAbs -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $ProjectDest -Recurse -Force
    }
    if (Test-Path -LiteralPath (Join-Path $ProjectDest "Release")) {
        Remove-Item -LiteralPath (Join-Path $ProjectDest "Release") -Recurse -Force
    }

    $BuildSpec = "$ProjectName/$Configuration"
    $NativeArgs = @(
        "-nosplash",
        "-data", (Quote-NativeArg $WorkspaceAbs),
        "-application", "org.eclipse.cdt.managedbuilder.core.headlessbuild",
        "-import", (Quote-NativeArg $ProjectDest),
        "-cleanBuild", (Quote-NativeArg $BuildSpec)
    ) -join " "

    Write-Host "Command   : `"$IdeExe`" $NativeArgs"
    Write-Host ""
    $StartedAt = Get-Date
    $Process = Start-Process -FilePath $IdeExe `
        -ArgumentList $NativeArgs `
        -Wait `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $StdoutPath `
        -RedirectStandardError $StderrPath
    $IdeExit = $Process.ExitCode

    # 合并 IDE 的 stdout/stderr；不同 Eclipse 版本输出编码可能不同，仅作日志保留。
    $Combined = New-Object System.Text.StringBuilder
    foreach ($part in @($StdoutPath, $StderrPath)) {
        if (Test-Path -LiteralPath $part) {
            try {
                [void]$Combined.Append((Get-Content -LiteralPath $part -Raw -ErrorAction Stop))
            } catch {
                [void]$Combined.AppendLine("[无法读取日志片段 $part]")
            }
        }
    }
    [System.IO.File]::WriteAllText(
        $LogAbs,
        $Combined.ToString(),
        (New-Object System.Text.UTF8Encoding($false))
    )
    Remove-Item -LiteralPath $StdoutPath, $StderrPath -Force -ErrorAction SilentlyContinue

    $SoPath = Join-Path (Join-Path $ProjectDest $Configuration) "libzkgui.so"
    $Built = $false
    $SoSize = 0
    if (Test-Path -LiteralPath $SoPath -PathType Leaf) {
        $SoItem = Get-Item -LiteralPath $SoPath
        if ($SoItem.Length -gt 0) {
            $Built = $true
            $SoSize = $SoItem.Length
        }
    }

    Write-Host ""
    Write-Host "IDE exit code: $IdeExit (clean 失败不代表 build 失败，以产物为准)"
    if (Test-Path -LiteralPath $LogAbs) {
        Write-Host "---- build log tail ----"
        Get-Content -LiteralPath $LogAbs -Tail 30 | ForEach-Object { Write-Host $_ }
        Write-Host "------------------------"
    }

    if (-not $Built) {
        Write-Host ""
        Write-Error "构建失败：未生成 $SoPath" -ErrorAction Continue
        Write-Host "完整日志：$LogAbs"
        exit 1
    }

    $Elapsed = (Get-Date) - $StartedAt
    Write-Host ""
    Write-Host ("构建成功：{0}" -f $SoPath)
    Write-Host ("libzkgui.so 大小：{0:N0} 字节（{1:N1} KB）" -f $SoSize, ($SoSize / 1KB))
    Write-Host ("耗时：{0:N1} 秒" -f $Elapsed.TotalSeconds)
    Write-Host "完整日志：$LogAbs"
    exit 0
} catch {
    Write-Host ""
    Write-Error $_.Exception.Message -ErrorAction Continue
    if ([string]::IsNullOrWhiteSpace($LogAbs)) {
        Write-Host "日志路径（若已产生）：$(Join-Path (Join-Path $RepoRoot 'build') 'headless-build.log')"
    } else {
        Write-Host "日志路径（若已产生）：$LogAbs"
    }
    exit 1
}
