#requires -Version 5.1
<#
.SYNOPSIS
    以官方 stock /res 为基线，生成 TC002（Z21）的 /res 分区 update.img。

.DESCRIPTION
    镜像内容 = build/flash/res-stock 的完整副本，仅替换：
      lib/libzkgui.so  <- -SoPath
      ui/main.ftu      <- -MainFtu
    其余 stock 文件（etc/EasyUI.cfg、libAEC_LINUX.so、libAPC_LINUX.so、
    ui/、tr/、font/、bin/）原样保留。etc/EasyUI.cfg 必须是 /res 路径，
    脚本不会把它改成 /tmp。

    打包命令（与 FlyThings IDE 内部调用一致）：
      zkswe_mkimg.exe imgout res.tmp -noappend -force-uid 1000 -force-gid 1000 -comp xz
      fsimg.exe -i res:res.tmp -o update.img -p 0x000606

    本脚本只生成本地镜像，不连接设备、不 adb push、不 setprop。

.PARAMETER SoPath
    libzkgui.so 输入，默认 IDE Release 产物。

.PARAMETER MainFtu
    main.ftu 输入，默认 native/MultiTimer/ui/main.ftu。

.PARAMETER OutDir
    最终 update.img（以及 update.img.sha256、build-info.txt）的输出目录。
    默认 dist。中间 res.tmp 留在 WorkDir，便于核对。

.PARAMETER StockRes
    stock /res 基线目录，默认 build/flash/res-stock。

.PARAMETER ToolsDir
    含 zkswe_mkimg.exe / fsimg.exe 的目录，默认 build/ide-tools。

.PARAMETER WorkDir
    暂存与打包工作目录。只允许位于仓库 build/ 下，默认 build/flash/image-work。

.PARAMETER DryRun
    只做输入/基线校验和 staging，打印将要执行的命令；不调用 mkimg/fsimg，
    不创建 OutDir、不写 update.img。staging 使用 WorkDir 的 -dryrun 同级目录，
    不会删除上一次正式打包的 res.tmp/日志。

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\make-image.ps1 -DryRun

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\make-image.ps1 `
        -SoPath dist\lib\libzkgui.so -OutDir build\flash\dryrun
#>

param(
    [string]$SoPath = "build/ide-workspace/MultiTimer/Release/libzkgui.so",
    [string]$MainFtu = "native/MultiTimer/ui/main.ftu",
    [string]$OutDir = "dist",
    [string]$StockRes = "build/flash/res-stock",
    [string]$ExtraRes = "native/MultiTimer/res-extra",
    [string]$ToolsDir = "build/ide-tools",
    [string]$WorkDir = "build/flash/image-work",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

$PartitionBytes = 8MB
$PlatformHex = "0x000606"
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "== $Message =="
}

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

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-TextSha256 {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return (($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString("X2") }) -join "")
    } finally {
        $sha.Dispose()
    }
}

function Get-TreeFingerprint {
    param([Parameter(Mandatory = $true)][string]$Root)
    $prefix = $Root.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $lines = @(
        Get-ChildItem -LiteralPath $Root -Recurse -File -Force | ForEach-Object {
            $rel = $_.FullName.Substring($prefix.Length).Replace('\', '/')
            "{0}`t{1}`t{2}" -f $rel, $_.Length, (Get-FileSha256 -Path $_.FullName)
        } | Sort-Object
    )
    return Get-TextSha256 -Text ($lines -join "`n")
}

function Invoke-ImageTool {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$LogPrefix
    )

    $stdout = "$LogPrefix.stdout.log"
    $stderr = "$LogPrefix.stderr.log"
    foreach ($p in @($stdout, $stderr)) {
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
    }

    $proc = Start-Process -FilePath $Exe `
        -ArgumentList $Arguments `
        -WorkingDirectory $WorkingDirectory `
        -Wait `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr

    $combined = New-Object System.Text.StringBuilder
    foreach ($p in @($stdout, $stderr)) {
        if (Test-Path -LiteralPath $p) {
            try {
                [void]$combined.AppendLine((Get-Content -LiteralPath $p -Raw -ErrorAction Stop))
            } catch {
                [void]$combined.AppendLine("[无法读取工具日志 $p]")
            }
        }
    }
    [System.IO.File]::WriteAllText("$LogPrefix.log", $combined.ToString(), $Utf8NoBom)

    Write-Host "---- $(Split-Path -Leaf $LogPrefix) (tail) ----"
    if (Test-Path -LiteralPath "$LogPrefix.log") {
        Get-Content -LiteralPath "$LogPrefix.log" -Tail 20 | ForEach-Object { Write-Host $_ }
    }
    Write-Host "---- exit code: $($proc.ExitCode) ----"
    return $proc.ExitCode
}

function Assert-File {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "缺少${Label}：$Path"
    }
    if ((Get-Item -LiteralPath $Path).Length -le 0) {
        throw "${Label}为空：$Path"
    }
}

try {
    $SoAbs = Resolve-RepoPath -Path $SoPath -Base $RepoRoot
    $FtuAbs = Resolve-RepoPath -Path $MainFtu -Base $RepoRoot
    $OutDirAbs = Resolve-RepoPath -Path $OutDir -Base $RepoRoot
    $StockAbs = Resolve-RepoPath -Path $StockRes -Base $RepoRoot
    $ExtraAbs = Resolve-RepoPath -Path $ExtraRes -Base $RepoRoot
    $ToolsAbs = Resolve-RepoPath -Path $ToolsDir -Base $RepoRoot
    $WorkAbs = Resolve-RepoPath -Path $WorkDir -Base $RepoRoot
    if ($DryRun) {
        # DryRun 不得删除正式打包的 res.tmp / 日志；用独立 staging 目录。
        $WorkAbs = $WorkAbs + "-dryrun"
    }

    $MkimgAbs = Join-Path $ToolsAbs "zkswe_mkimg.exe"
    $FsimgAbs = Join-Path $ToolsAbs "fsimg.exe"
    $BuildRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "build"))
    $BuildPrefix = $BuildRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar

    Write-Host "== TC002 /res image builder =="
    Write-Host "Repo      : $RepoRoot"
    Write-Host "Stock /res: $StockAbs"
    Write-Host "Input .so : $SoAbs"
    Write-Host "Input ftu : $FtuAbs"
    Write-Host "OutDir    : $OutDirAbs"
    Write-Host "WorkDir   : $WorkAbs"
    Write-Host "Platform  : $PlatformHex (Z21, 8 MiB res partition)"
    if ($DryRun) { Write-Host "Mode      : DRY RUN（不调用打包工具、不创建 OutDir）" }

    Write-Step "校验输入与 stock 基线"
    Assert-File -Path $MkimgAbs -Label "zkswe_mkimg.exe"
    Assert-File -Path $FsimgAbs -Label "fsimg.exe"
    Assert-File -Path $SoAbs -Label "libzkgui.so"
    Assert-File -Path $FtuAbs -Label "main.ftu"

    if (-not (Test-Path -LiteralPath $StockAbs -PathType Container)) {
        throw "缺少 stock /res 基线：$StockAbs（先按 docs/FLASH-RUNBOOK.md 用 adb pull 拉取）"
    }
    foreach ($rel in @(
        "etc\EasyUI.cfg",
        "lib\libzkgui.so",
        "lib\libAEC_LINUX.so",
        "lib\libAPC_LINUX.so",
        "ui\main.ftu",
        "bin\gattserverbin"
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $StockAbs $rel) -PathType Leaf)) {
            throw "stock /res 不完整，缺少：$rel"
        }
    }
    foreach ($dir in @("font", "tr", "ui\font_image", "ui\web")) {
        if (-not (Test-Path -LiteralPath (Join-Path $StockAbs $dir) -PathType Container)) {
            throw "stock /res 不完整，缺少目录：$dir"
        }
    }

    $CfgPath = Join-Path $StockAbs "etc\EasyUI.cfg"
    $CfgText = [System.IO.File]::ReadAllText($CfgPath, [System.Text.Encoding]::UTF8)
    try {
        $Cfg = $CfgText | ConvertFrom-Json
    } catch {
        throw "stock etc/EasyUI.cfg 不是合法 JSON：$($_.Exception.Message)"
    }
    if ([string]$Cfg.startupLibPath -ne "/res/lib/libzkgui.so") {
        throw "stock EasyUI.cfg startupLibPath 必须为 /res/lib/libzkgui.so，实际为：$($Cfg.startupLibPath)"
    }
    if (-not ([string]$Cfg.resPath).StartsWith("/res/")) {
        throw "stock EasyUI.cfg resPath 必须位于 /res/ 下，实际为：$($Cfg.resPath)"
    }
    if ($null -ne $Cfg.languagePath -and -not ([string]$Cfg.languagePath).StartsWith("/res/")) {
        throw "stock EasyUI.cfg languagePath 必须位于 /res/ 下，实际为：$($Cfg.languagePath)"
    }
    if ($CfgText -match '/tmp/') {
        throw "stock EasyUI.cfg 含 /tmp/ 路径，拒绝把它固化到 /res"
    }

    $soItem = Get-Item -LiteralPath $SoAbs
    $ftuItem = Get-Item -LiteralPath $FtuAbs
    $soBytes = [System.IO.File]::ReadAllBytes($SoAbs)
    if ($soBytes.Length -lt 52) { throw "libzkgui.so 太小，疑似无效 ELF：$SoAbs" }
    if ($soBytes[0] -ne 0x7F -or $soBytes[1] -ne 0x45 -or $soBytes[2] -ne 0x4C -or $soBytes[3] -ne 0x46) {
        throw "libzkgui.so 不是 ELF 文件：$SoAbs"
    }
    if ($soBytes[4] -ne 1 -or $soBytes[5] -ne 1) {
        throw "libzkgui.so 不是 32-bit little-endian ELF（class=$($soBytes[4]), data=$($soBytes[5])）"
    }
    $machine = [int]$soBytes[18] -bor ([int]$soBytes[19] -shl 8)
    if ($machine -ne 40) {
        throw ("libzkgui.so 不是 ARM（e_machine=0x{0:X4}，期望 0x0028）：{1}" -f $machine, $SoAbs)
    }

    $StockFiles = @(Get-ChildItem -LiteralPath $StockAbs -Recurse -File -Force)
    $StockTreeHash = Get-TreeFingerprint -Root $StockAbs
    $ExtraFiles = @()
    if (Test-Path -LiteralPath $ExtraAbs -PathType Container) {
        $ExtraFiles = @(Get-ChildItem -LiteralPath $ExtraAbs -Recurse -File -Force)
        Write-Host ("额外 /res 文件: {0}（{1}）" -f $ExtraFiles.Count, $ExtraAbs)
    }
    if ($ExtraFiles.Count -gt 0 -and -not (Test-Path -LiteralPath $ExtraAbs -PathType Container)) {
        throw "ExtraRes 存在但无法读取：$ExtraAbs"
    }
    $SoHash = Get-FileSha256 -Path $SoAbs
    $FtuHash = Get-FileSha256 -Path $FtuAbs
    $CfgHash = Get-FileSha256 -Path $CfgPath

    Write-Host ("stock 文件数 : {0}" -f $StockFiles.Count)
    Write-Host ("stock 字节数 : {0:N0}" -f (($StockFiles | Measure-Object -Property Length -Sum).Sum))
    Write-Host ("stock 指纹   : {0}" -f $StockTreeHash)
    Write-Host ("libzkgui.so  : {0:N0} bytes  SHA256 {1}" -f $soItem.Length, $SoHash)
    Write-Host ("main.ftu     : {0:N0} bytes  SHA256 {1}" -f $ftuItem.Length, $FtuHash)
    Write-Host ("EasyUI.cfg   : SHA256 {0}" -f $CfgHash)

    if (-not $WorkAbs.StartsWith($BuildPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "WorkDir 必须位于仓库 build/ 下，拒绝创建/删除：$WorkAbs"
    }
    if ($WorkAbs.TrimEnd('\') -eq $StockAbs.TrimEnd('\')) {
        throw "WorkDir 不能与 StockRes 相同"
    }

    Write-Step "staging（stock 副本 + 仅替换两个文件）"
    if (Test-Path -LiteralPath $WorkAbs) {
        Remove-Item -LiteralPath $WorkAbs -Recurse -Force
    }
    New-Item -ItemType Directory -Path $WorkAbs -Force | Out-Null
    $ImgOut = Join-Path $WorkAbs "imgout"
    New-Item -ItemType Directory -Path $ImgOut -Force | Out-Null

    Get-ChildItem -LiteralPath $StockAbs -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $ImgOut -Recurse -Force
    }
    if (Test-Path -LiteralPath $ExtraAbs -PathType Container) {
        Get-ChildItem -LiteralPath $ExtraAbs -Force | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $ImgOut -Recurse -Force
        }
    }
    Copy-Item -LiteralPath $SoAbs -Destination (Join-Path $ImgOut "lib\libzkgui.so") -Force
    Copy-Item -LiteralPath $FtuAbs -Destination (Join-Path $ImgOut "ui\main.ftu") -Force

    foreach ($dir in @("font", "tr", "ui\font_image", "ui\web")) {
        if (-not (Test-Path -LiteralPath (Join-Path $ImgOut $dir) -PathType Container)) {
            throw "staging 丢失目录：$dir"
        }
    }
    $StagedFiles = @(Get-ChildItem -LiteralPath $ImgOut -Recurse -File -Force)
    $ExpectedStagedCount = $StockFiles.Count + $ExtraFiles.Count
    if ($StagedFiles.Count -ne $ExpectedStagedCount) {
        throw "staging 文件数 $($StagedFiles.Count) 与 stock $($StockFiles.Count) + extra $($ExtraFiles.Count) 不一致"
    }
    if ((Get-FileSha256 -Path (Join-Path $ImgOut "lib\libzkgui.so")) -ne $SoHash) {
        throw "staging libzkgui.so 哈希与输入不一致"
    }
    if ((Get-FileSha256 -Path (Join-Path $ImgOut "ui\main.ftu")) -ne $FtuHash) {
        throw "staging main.ftu 哈希与输入不一致"
    }
    if ((Get-FileSha256 -Path (Join-Path $ImgOut "etc\EasyUI.cfg")) -ne $CfgHash) {
        throw "staging EasyUI.cfg 被意外修改"
    }
    $StagedTreeHash = Get-TreeFingerprint -Root $ImgOut
    Write-Host ("staged 文件数: {0}" -f $StagedFiles.Count)
    Write-Host ("staged 指纹  : {0}" -f $StagedTreeHash)
    Write-Host "已保留: etc/EasyUI.cfg, lib/libAEC_LINUX.so, lib/libAPC_LINUX.so, ui/*, bin/*, font/*, tr/*"
    Write-Host "已替换: lib/libzkgui.so, ui/main.ftu"

    $MkimgArgs = @(
        "imgout",
        "res.tmp",
        "-noappend",
        "-force-uid", "1000",
        "-force-gid", "1000",
        "-comp", "xz"
    )
    $FsimgArgs = @(
        "-i", "res:res.tmp",
        "-o", "update.img",
        "-p", $PlatformHex
    )

    if ($DryRun) {
        Write-Step "DRY RUN：将执行的命令"
        Write-Host ("cd `"{0}`"" -f $WorkAbs)
        Write-Host ("`"{0}`" {1}" -f $MkimgAbs, ($MkimgArgs -join " "))
        Write-Host ("`"{0}`" {1}" -f $FsimgAbs, ($FsimgArgs -join " "))
        Write-Host ""
        Write-Host "DRY RUN 完成：未调用打包工具，未创建 $OutDirAbs，未写 update.img。"
        Write-Host "staging 保留在：$ImgOut"
        exit 0
    }

    Write-Step "生成 squashfs：res.tmp"
    $mkExit = Invoke-ImageTool -Exe $MkimgAbs -Arguments $MkimgArgs -WorkingDirectory $WorkAbs -LogPrefix (Join-Path $WorkAbs "zkswe_mkimg")
    $ResTmp = Join-Path $WorkAbs "res.tmp"
    if ($mkExit -ne 0) {
        throw "zkswe_mkimg.exe 失败（exit=$mkExit），完整日志：$(Join-Path $WorkAbs 'zkswe_mkimg.log')"
    }
    Assert-File -Path $ResTmp -Label "res.tmp"
    $ResItem = Get-Item -LiteralPath $ResTmp
    $resMagic = [System.IO.File]::ReadAllBytes($ResTmp)
    if ($resMagic.Length -lt 4 -or $resMagic[0] -ne 0x68 -or $resMagic[1] -ne 0x73 -or $resMagic[2] -ne 0x71 -or $resMagic[3] -ne 0x73) {
        throw "res.tmp 不是 squashfs（缺少 hsqs magic）：$ResTmp"
    }
    if ($ResItem.Length -gt $PartitionBytes) {
        throw ("res.tmp {0:N0} bytes 超过 8 MiB 分区上限 {1:N0} bytes" -f $ResItem.Length, $PartitionBytes)
    }
    $ResPercent = [math]::Round(100.0 * $ResItem.Length / $PartitionBytes, 2)
    $ResHash = Get-FileSha256 -Path $ResTmp
    Write-Host ("res.tmp: {0:N0} bytes（8 MiB 的 {1}%），SHA256 {2}" -f $ResItem.Length, $ResPercent, $ResHash)

    Write-Step "封装 update.img"
    $fsExit = Invoke-ImageTool -Exe $FsimgAbs -Arguments $FsimgArgs -WorkingDirectory $WorkAbs -LogPrefix (Join-Path $WorkAbs "fsimg")
    $UpdateTmp = Join-Path $WorkAbs "update.img"
    if ($fsExit -ne 0) {
        throw "fsimg.exe 失败（exit=$fsExit），完整日志：$(Join-Path $WorkAbs 'fsimg.log')"
    }
    Assert-File -Path $UpdateTmp -Label "update.img"
    $UpdateItem = Get-Item -LiteralPath $UpdateTmp
    $updateBytes = [System.IO.File]::ReadAllBytes($UpdateTmp)
    if ($updateBytes.Length -lt 9) { throw "update.img 太小：$UpdateTmp" }
    $magic = [System.Text.Encoding]::ASCII.GetString($updateBytes, 0, 9)
    if ($magic -ne "ZKSWEV1.0") {
        throw "update.img 头部 magic 不是 ZKSWEV1.0（实际：$magic）"
    }
    $UpdateHash = Get-FileSha256 -Path $UpdateTmp
    Write-Host ("update.img: {0:N0} bytes，SHA256 {1}" -f $UpdateItem.Length, $UpdateHash)

    Write-Step "发布到 OutDir"
    if (Test-Path -LiteralPath $OutDirAbs -PathType Leaf) {
        throw "OutDir 是文件而不是目录：$OutDirAbs"
    }
    if (-not (Test-Path -LiteralPath $OutDirAbs -PathType Container)) {
        New-Item -ItemType Directory -Path $OutDirAbs -Force | Out-Null
    }
    $FinalImage = Join-Path $OutDirAbs "update.img"
    $FinalNew = Join-Path $OutDirAbs "update.img.new"
    Copy-Item -LiteralPath $UpdateTmp -Destination $FinalNew -Force
    Move-Item -LiteralPath $FinalNew -Destination $FinalImage -Force

    $ChecksumPath = Join-Path $OutDirAbs "update.img.sha256"
    [System.IO.File]::WriteAllText($ChecksumPath, ("{0}  update.img`n" -f $UpdateHash), $Utf8NoBom)

    $infoLines = @(
        "TC002 / Z21 /res update image",
        "generated_local=$([DateTimeOffset]::Now.ToString('o'))",
        "platform=$PlatformHex",
        "partition=res (mtd3, 8 MiB)",
        "stock_res=$StockAbs",
        "stock_files=$($StockFiles.Count)",
        "stock_tree_sha256=$StockTreeHash",
        "extra_res=$ExtraAbs",
        "extra_files=$($ExtraFiles.Count)",
        "extra_tree_sha256=$(if ($ExtraFiles.Count -gt 0) { Get-TreeFingerprint -Root $ExtraAbs } else { '' })",
        "input_so=$SoAbs",
        "input_so_bytes=$($soItem.Length)",
        "input_so_sha256=$SoHash",
        "input_ftu=$FtuAbs",
        "input_ftu_bytes=$($ftuItem.Length)",
        "input_ftu_sha256=$FtuHash",
        "easyui_cfg_sha256=$CfgHash",
        "staged_files=$($StagedFiles.Count)",
        "staged_tree_sha256=$StagedTreeHash",
        "res_tmp=$ResTmp",
        "res_tmp_bytes=$($ResItem.Length)",
        "res_tmp_budget_bytes=$PartitionBytes",
        "res_tmp_sha256=$ResHash",
        "update_img=$FinalImage",
        "update_img_bytes=$($UpdateItem.Length)",
        "update_img_sha256=$UpdateHash",
        "zkswe_mkimg_sha256=$(Get-FileSha256 -Path $MkimgAbs)",
        "fsimg_sha256=$(Get-FileSha256 -Path $FsimgAbs)",
        "mkimg_command=zkswe_mkimg.exe $($MkimgArgs -join ' ')",
        "fsimg_command=fsimg.exe $($FsimgArgs -join ' ')"
    )
    $InfoPath = Join-Path $OutDirAbs "build-info.txt"
    [System.IO.File]::WriteAllText($InfoPath, (($infoLines -join "`n") + "`n"), $Utf8NoBom)

    Write-Step "完成"
    Write-Host "镜像     : $FinalImage"
    Write-Host ("大小     : {0:N0} bytes（{1:N2} MiB）" -f $UpdateItem.Length, ($UpdateItem.Length / 1MB))
    Write-Host "SHA256   : $UpdateHash"
    Write-Host "校验文件 : $ChecksumPath"
    Write-Host "构建日志 : $InfoPath"
    Write-Host "res.tmp  : $ResTmp"
    Write-Host ""
    Write-Host "下一步（仍需人工确认；本脚本不会刷机）："
    Write-Host "  powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 -Image `"$FinalImage`""
    Write-Host "确认无误后，才可显式加 -Confirm 执行刷写。"
    exit 0
} catch {
    Write-Host ""
    Write-Host "错误：$($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
