#requires -Version 5.1
<#
.SYNOPSIS
    TC002（Z21）官方 ADB 固化流程：把 /res 的 update.img 写入设备。

.DESCRIPTION
    默认是 DryRun：只做只读预检（设备型号、/tmp 空间、备份、镜像头），
    打印将执行的命令，绝不 push、不 setprop。

    只有显式传入 -Confirm 才会执行官方流程：
      adb push update.img /tmp/update.img
      adb shell setprop sys.zkupgrade.flag 255
      adb shell setprop sys.zkupgrade.dir /tmp
      adb shell setprop ctl.restart zkswe

    硬性约束：
      - 只写 /tmp/update.img；脚本没有写 /data 的任何代码路径。
      - 不 adb root / remount / dd，不直接写 /res 或 /dev/block。
      - 设备型号必须是 Zkswe_SSD21X_SPINOR。
      - 刷机失败时唯一可靠的官方恢复手段是长按复位键约 5 秒
        （恢复官方固件，同时清空 /data）。

.PARAMETER DeviceIp
    WiFi ADB 设备 IP，默认 192.168.31.124（端口固定 5555）。

.PARAMETER Image
    update.img 路径，默认 dist/update.img。

.PARAMETER Adb
    adb.exe 路径，默认 tools/platform-tools/adb.exe。

.PARAMETER BackupDir
    刷机前必须存在的备份目录，默认 build/device-backup。

.PARAMETER TimeoutSec
    触发升级后等待设备重启/ADB 恢复的秒数，默认 300。

.PARAMETER SkipHttpCheck
    恢复后不等待 http://<ip>:8080/api/status，只做 ADB / /res 检查。

.PARAMETER Confirm
    *** 真正的写设备开关 ***。不传时永远只做 DryRun。

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 `
        -Image build\flash\dryrun\update.img

.EXAMPLE
    # 仅在人工复核 DryRun 输出、确认备份与风险后执行：
    powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 -Confirm
#>

param(
    [string]$DeviceIp = "192.168.31.124",
    [string]$Image = "dist/update.img",
    [string]$Adb = "tools/platform-tools/adb.exe",
    [string]$BackupDir = "build/device-backup",
    [int]$TimeoutSec = 300,
    [switch]$SkipHttpCheck,
    [switch]$Confirm
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Serial = "${DeviceIp}:5555"
$RemoteImage = "/tmp/update.img"
$PartitionBytes = 8MB
$UpdateHeaderBytes = 572
$DryRun = -not $Confirm

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

function Assert-File {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label,
        [long]$MinBytes = 1
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "缺少${Label}：$Path"
    }
    $len = (Get-Item -LiteralPath $Path).Length
    if ($len -lt $MinBytes) {
        throw "${Label}大小异常（$len bytes，至少需要 $MinBytes）：$Path"
    }
}

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Invoke-Adb {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [switch]$AllowFailure
    )
    # adb 会把 push 进度写到 stderr；在 $ErrorActionPreference=Stop 下，
    # PowerShell 5.1 会把 native stderr 当成终止错误，导致成功也被误判。
    # 这里临时改为 Continue，只用退出码判定成败，再恢复原设置。
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = @(& $AdbAbs @Arguments 2>&1)
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedEap
    }
    if ($code -ne 0 -and -not $AllowFailure) {
        throw ("adb {0} 失败（exit={1}）：{2}" -f ($Arguments -join " "), $code, (($out | ForEach-Object { "$_" }) -join "`n"))
    }
    return [pscustomobject]@{
        ExitCode = $code
        Lines    = $out
        Text     = (($out | ForEach-Object { "$_" }) -join "`n")
    }
}

function Show-Verification {
    Write-Host ""
    Write-Host "== 验证命令（只读） =="
    Write-Host "  1) App HTTP 状态："
    Write-Host ("     Invoke-WebRequest -UseBasicParsing http://{0}:8080/api/status | Select-Object -ExpandProperty Content" -f $DeviceIp)
    Write-Host "  2) /res 文件与时间戳："
    Write-Host ("     `"{0}`" -s {1} shell `"ls -l /res/etc/EasyUI.cfg /res/lib/libzkgui.so /res/ui/main.ftu`"" -f $AdbAbs, $Serial)
    Write-Host "  3) 确认 EasyUI.cfg 仍指向 /res："
    Write-Host ("     `"{0}`" -s {1} shell `"cat /res/etc/EasyUI.cfg`"" -f $AdbAbs, $Serial)
    Write-Host "  4) 服务/防砖标志/分区："
    Write-Host ("     `"{0}`" -s {1} shell `"getprop init.svc.zkswe; getprop sys.zkapp.state; getprop sys.zkupgrade.flag; df -k /res /data /tmp`"" -f $AdbAbs, $Serial)
    Write-Host "  5) 日志："
    Write-Host ("     `"{0}`" -s {1} logcat -d -v time" -f $AdbAbs, $Serial)
}

function Show-Rollback {
    Write-Host ""
    Write-Host "== 回滚 / 救砖 =="
    Write-Host "  * 首选（官方恢复路径）：按住 USB-C 旁的复位键上电/保持按住，等待设备恢复官方固件。"
    Write-Host "    社区资料与任务约定为约 5 秒；官方 README 只写了“按住复位键上电自动刷回”。"
    Write-Host "    恢复会清空 /data：WiFi 凭据、multitimer.conf、时钟状态都会丢失。"
    Write-Host "  * 复位后需重新用官方 App 配网；本应用不再存在，需重新刷入。"
    Write-Host "  * 不要把 build/device-backup/res-stock.img 当作 update.img 刷入（它是 8 MiB 原始分区快照，"
    Write-Host "    不是官方升级包，官方 ADB/TF 卡升级流程不接受它）。"
    Write-Host "  * 不要 adb push 到 /data（JFFS2 会卡死 USB）；也不要尝试直接写 /dev/block/mtdblock3。"
    Write-Host "  * 若设备还能进官方恢复/升级界面，可尝试 TF 卡 FAT32 根目录放官方 update.img；"
    Write-Host "    本仓库的 _mnt_storage_update.img 只是候选备份，未在真机验证。"
}

function Show-DeviceSummary {
    param([string]$Model, [string]$FreeKb)
    Write-Host "设备型号 : $Model"
    Write-Host "ADB 序列 : $Serial"
    Write-Host "/tmp 可用: $FreeKb KB"
}

try {
    $ImageAbs = Resolve-RepoPath -Path $Image -Base $RepoRoot
    $AdbAbs = Resolve-RepoPath -Path $Adb -Base $RepoRoot
    $BackupAbs = Resolve-RepoPath -Path $BackupDir -Base $RepoRoot

    Write-Host "== TC002 /res update flasher =="
    Write-Host "模式     : $(if ($DryRun) { 'DRY RUN（默认，禁止写设备）' } else { '*** CONFIRM：将写设备 ***' })"
    Write-Host "Image    : $ImageAbs"
    Write-Host "Device   : $Serial"
    Write-Host "ADB      : $AdbAbs"
    Write-Host "Backups  : $BackupAbs"
    Write-Host "Remote   : $RemoteImage  (仅 /tmp；/data 不会被触碰)"

    Write-Step "预检 1/4：本地镜像与备份"
    Assert-File -Path $AdbAbs -Label "adb.exe" -MinBytes 100000
    # 保留完整 stock 资源的正常镜像约 0.7 MiB；512 KiB 下限用于挡住旧的
    # 160 KiB 最小镜像（只含 3 个文件，不保留 stock ui/lib/bin）。
    Assert-File -Path $ImageAbs -Label "update.img" -MinBytes 512KB

    $ImageItem = Get-Item -LiteralPath $ImageAbs
    $ImageHash = Get-FileSha256 -Path $ImageAbs
    $imageBytes = [System.IO.File]::ReadAllBytes($ImageAbs)
    if ($imageBytes.Length -le $UpdateHeaderBytes) {
        throw "update.img 太小，连 572-byte 头部都不完整：$($imageBytes.Length) bytes"
    }
    $magic = [System.Text.Encoding]::ASCII.GetString($imageBytes, 0, 9)
    if ($magic -ne "ZKSWEV1.0") {
        throw "update.img 头部 magic 不是 ZKSWEV1.0（实际：$magic），拒绝刷写"
    }
    $payloadBytes = $imageBytes.Length - $UpdateHeaderBytes
    if ($payloadBytes -lt 512KB) {
        throw ("update.img payload 仅 {0:N0} bytes，疑似不含完整 stock /res（正常约 0.7 MiB），拒绝刷写" -f $payloadBytes)
    }
    if ($payloadBytes -gt $PartitionBytes) {
        throw ("update.img payload {0:N0} bytes 超过 8 MiB /res 分区" -f $payloadBytes)
    }
    if ($imageBytes.Length -gt ($PartitionBytes + 1MB)) {
        throw ("update.img {0:N0} bytes 异常大（/res 仅 8 MiB）" -f $imageBytes.Length)
    }

    $requiredBackups = @(
        @{ Name = "res-stock.img";                         Min = 8MB;    Why = "stock /res 原始分区快照（仅存档，不可直接当 update.img）" },
        @{ Name = "stock-libzkgui.so";                     Min = 1MB;    Why = "stock libzkgui.so" },
        @{ Name = "_mnt_storage_update.img";               Min = 1MB;    Why = "官方 update.img 候选备份" },
        @{ Name = "_res_etc_EasyUI.cfg";                   Min = 100;    Why = "stock /res/etc/EasyUI.cfg" },
        @{ Name = "_data_setting.ini";                     Min = 1;      Why = "官方 App 设置（可能含敏感凭据，勿外传）" },
        @{ Name = "_data_misc_wifi_wpa_supplicant.conf";   Min = 1;      Why = "WiFi 配置备份（可能含敏感凭据，勿外传）" }
    )
    $backupRows = @()
    foreach ($b in $requiredBackups) {
        $p = Join-Path $BackupAbs $b.Name
        Assert-File -Path $p -Label ("备份 " + $b.Name) -MinBytes $b.Min
        $backupRows += [pscustomobject]@{
            File = $b.Name
            Bytes = (Get-Item -LiteralPath $p).Length
            Why = $b.Why
        }
    }

    Write-Host ("update.img : {0:N0} bytes，payload {1:N0} bytes，SHA256 {2}" -f $ImageItem.Length, $payloadBytes, $ImageHash)
    $backupRows | Format-Table File, Bytes -AutoSize | Out-Host
    Write-Host "备份校验通过；/data 不会在本流程中被写入。"

    Write-Step "预检 2/4：WiFi ADB 与设备型号"
    $connect = Invoke-Adb -Arguments @("connect", $Serial) -AllowFailure
    Write-Host $connect.Text
    Start-Sleep -Milliseconds 500

    $state = Invoke-Adb -Arguments @("-s", $Serial, "get-state") -AllowFailure
    $stateText = ($state.Text -replace "\s", "")
    if ($state.ExitCode -ne 0 -or $stateText -ne "device") {
        throw ("ADB 设备未就绪（state={0}）。先确认设备与电脑同网、已开启 WiFi ADB 5555，并尝试：`"{1}`" connect {2}" -f $state.Text.Trim(), $AdbAbs, $Serial)
    }

    $modelResult = Invoke-Adb -Arguments @("-s", $Serial, "shell", "getprop ro.product.model")
    $model = $modelResult.Text.Trim()
    if ($model -ne "Zkswe_SSD21X_SPINOR") {
        throw "设备型号不是 Zkswe_SSD21X_SPINOR（实际：$model），拒绝刷写"
    }

    Write-Step "预检 3/4：/tmp 空间"
    $dfResult = Invoke-Adb -Arguments @("-s", $Serial, "shell", "df -k /tmp")
    $dfLine = @($dfResult.Lines | ForEach-Object { "$_" } | Where-Object {
        $_ -match '^\s*\S+\s+\d+\s+\d+\s+\d+'
    } | Select-Object -Last 1)
    if (-not $dfLine) {
        throw "/tmp 空间解析失败，拒绝继续：$($dfResult.Text)"
    }
    $dfParts = ($dfLine[0].Trim() -split '\s+')
    $freeKb = [int64]$dfParts[3]
    $needKb = [int64]([math]::Ceiling($ImageItem.Length / 1024.0)) + 2048
    if ($freeKb -lt $needKb) {
        throw ("/tmp 空间不足：可用 {0} KB，需要 {1} KB（镜像 + 2 MiB 余量）" -f $freeKb, $needKb)
    }
    Show-DeviceSummary -Model $model -FreeKb $freeKb

    Write-Step "预检 4/4：当前升级属性（只读）"
    $flag = Invoke-Adb -Arguments @("-s", $Serial, "shell", "getprop sys.zkupgrade.flag") -AllowFailure
    $flagText = $flag.Text.Trim()
    if ([string]::IsNullOrEmpty($flagText)) { $flagText = "(empty)" }
    Write-Host "sys.zkupgrade.flag = $flagText"
    if ($flagText -notin @("(empty)", "0")) {
        Write-Warning "设备上已有非零 sys.zkupgrade.flag=$flagText；如果它不是预期状态，先停止并人工排查。"
    }

    Write-Step "预检通过"
    Write-Host "唯一会被写入的设备路径：$RemoteImage"
    Write-Host "不会写 /data；不会写 /res；不会 adb root/remount/dd。"

    if ($DryRun) {
        Write-Step "DRY RUN：将执行的官方 ADB 流程（当前不会执行）"
        Write-Host ("  `"{0}`" -s {1} push `"{2}`" {3}" -f $AdbAbs, $Serial, $ImageAbs, $RemoteImage)
        Write-Host ("  `"{0}`" -s {1} shell `"setprop sys.zkupgrade.flag 255`"" -f $AdbAbs, $Serial)
        Write-Host ("  `"{0}`" -s {1} shell `"setprop sys.zkupgrade.dir /tmp`"" -f $AdbAbs, $Serial)
        Write-Host ("  `"{0}`" -s {1} shell `"setprop ctl.restart zkswe`"" -f $AdbAbs, $Serial)
        Write-Host ""
        Write-Host "预检成功。要真正刷写，必须显式执行："
        Write-Host ("  powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 -Image `"{0}`" -Confirm" -f $ImageAbs)
        Write-Host "执行前请再读一遍 docs/FLASH-RUNBOOK.md 与回滚说明。"
        Show-Verification
        Show-Rollback
        exit 0
    }

    Write-Step "执行：push -> setprop flag -> setprop dir -> restart"
    Write-Host "*** 即将写设备。请勿断电、勿拔线、勿关闭本窗口。***"
    Start-Sleep -Seconds 2

    Write-Host "push $ImageAbs -> $RemoteImage"
    $push = Invoke-Adb -Arguments @("-s", $Serial, "push", $ImageAbs, $RemoteImage)
    Write-Host $push.Text

    $remoteLs = Invoke-Adb -Arguments @("-s", $Serial, "shell", "ls -l $RemoteImage") -AllowFailure
    if ($remoteLs.ExitCode -ne 0) {
        throw "push 后无法读取 $RemoteImage：$($remoteLs.Text)"
    }
    $lsParts = (($remoteLs.Text.Trim()) -split '\s+')
    if ($lsParts.Count -ge 5) {
        $remoteBytes = [int64]$lsParts[4]
        if ($remoteBytes -ne $ImageItem.Length) {
            throw ("远端 update.img 大小 {0} 与本地 {1} 不一致，拒绝触发升级" -f $remoteBytes, $ImageItem.Length)
        }
        Write-Host ("远端文件大小校验通过：{0:N0} bytes" -f $remoteBytes)
    } else {
        Write-Warning "无法解析远端 ls 输出，继续但请人工核对：$($remoteLs.Text)"
    }

    $setFlag = Invoke-Adb -Arguments @("-s", $Serial, "shell", "setprop sys.zkupgrade.flag 255")
    if ($setFlag.Text.Trim()) { Write-Host $setFlag.Text }
    Write-Host "已设置 sys.zkupgrade.flag=255"

    $setDir = Invoke-Adb -Arguments @("-s", $Serial, "shell", "setprop sys.zkupgrade.dir /tmp")
    if ($setDir.Text.Trim()) { Write-Host $setDir.Text }
    Write-Host "已设置 sys.zkupgrade.dir=/tmp"

    $restart = Invoke-Adb -Arguments @("-s", $Serial, "shell", "setprop ctl.restart zkswe")
    if ($restart.Text.Trim()) { Write-Host $restart.Text }
    Write-Host "已触发 zkswe 重启；升级守护进程应开始写 /res 并重启设备。"

    Write-Step "等待设备重启与 ADB 恢复（超时 ${TimeoutSec}s）"
    Start-Sleep -Seconds 3
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $sawOffline = $false
    $recovered = $false
    $lastNotice = Get-Date
    $lastConnectAttempt = Get-Date

    while ((Get-Date) -lt $deadline) {
        $probe = Invoke-Adb -Arguments @("-s", $Serial, "shell", "cat /proc/uptime") -AllowFailure
        $uptimeText = ($probe.Text -replace "\s", "")
        $online = ($probe.ExitCode -eq 0 -and $uptimeText -match '^\d+\.\d+')

        if (-not $online) {
            if (-not $sawOffline) {
                Write-Host "检测到 ADB 断开/设备重启..."
                $sawOffline = $true
            }
            if (((Get-Date) - $lastConnectAttempt).TotalSeconds -ge 10) {
                Invoke-Adb -Arguments @("connect", $Serial) -AllowFailure | Out-Null
                $lastConnectAttempt = Get-Date
            }
            Start-Sleep -Seconds 2
            continue
        }

        if ($sawOffline) {
            $recovered = $true
            Write-Host "ADB 已恢复，uptime=$uptimeText"
            break
        }

        if (((Get-Date) - $lastNotice).TotalSeconds -ge 15) {
            Write-Host "等待升级触发重启...（尚未观察到 ADB 断开）"
            $lastNotice = Get-Date
        }
        Start-Sleep -Seconds 2
    }

    if (-not $recovered) {
        Write-Warning "等待超时：没有观察到“先断开再恢复”的完整重启。"
        Write-Warning "不要立刻断电。先看屏幕、再试 ADB 与 HTTP；若长时间黑屏/无 ADB，按复位键回滚。"
        Show-Verification
        Show-Rollback
        exit 2
    }

    Write-Step "恢复后检查"
    Start-Sleep -Seconds 2
    $svc = Invoke-Adb -Arguments @("-s", $Serial, "shell", "getprop init.svc.zkswe") -AllowFailure
    $appState = Invoke-Adb -Arguments @("-s", $Serial, "shell", "getprop sys.zkapp.state") -AllowFailure
    $upgradeFlag = Invoke-Adb -Arguments @("-s", $Serial, "shell", "getprop sys.zkupgrade.flag") -AllowFailure
    Write-Host ("init.svc.zkswe       = {0}" -f $svc.Text.Trim())
    Write-Host ("sys.zkapp.state      = {0}" -f $appState.Text.Trim())
    Write-Host ("sys.zkupgrade.flag   = {0}" -f $upgradeFlag.Text.Trim())

    $resLs = Invoke-Adb -Arguments @("-s", $Serial, "shell", "ls -l /res/etc/EasyUI.cfg /res/lib/libzkgui.so /res/ui/main.ftu") -AllowFailure
    Write-Host "---- /res 关键文件 ----"
    Write-Host $resLs.Text
    $cfgCat = Invoke-Adb -Arguments @("-s", $Serial, "shell", "cat /res/etc/EasyUI.cfg") -AllowFailure
    Write-Host "---- /res/etc/EasyUI.cfg ----"
    Write-Host $cfgCat.Text
    if ($cfgCat.Text -match '/tmp/') {
        Write-Warning "EasyUI.cfg 仍含 /tmp/ 路径；固化可能没有生效。"
    }

    $httpOk = $false
    if (-not $SkipHttpCheck) {
        Write-Host ""
        Write-Host "等待 http://${DeviceIp}:8080/api/status ..."
        $httpDeadline = (Get-Date).AddSeconds(60)
        while ((Get-Date) -lt $httpDeadline) {
            try {
                $resp = Invoke-WebRequest -UseBasicParsing -Uri ("http://{0}:8080/api/status" -f $DeviceIp) -TimeoutSec 3
                if ($resp.StatusCode -eq 200) {
                    $httpOk = $true
                    Write-Host "HTTP 200："
                    Write-Host $resp.Content
                    break
                }
            } catch {
                # App 可能还在启动，继续轮询。
            }
            Start-Sleep -Seconds 2
        }
        if (-not $httpOk) {
            Write-Warning "HTTP 8080 暂未返回 200；设备可能仍在启动，或新 App 未正常运行。"
        }
    }

    Show-Verification
    Show-Rollback

    if (-not $SkipHttpCheck -and -not $httpOk) {
        Write-Host ""
        Write-Host "升级流程已执行，但 HTTP 验证未通过。不要重复刷机；先收集 logcat/屏幕信息并评估是否回滚。" -ForegroundColor Yellow
        exit 3
    }

    Write-Host ""
    Write-Host "刷写流程完成：ADB 已恢复，/res 文件已检查$(if (-not $SkipHttpCheck) { '，HTTP 8080 正常' })。"
    Write-Host "如果显示/功能异常，按上面的复位键说明回滚。"
    exit 0
} catch {
    Write-Host ""
    Write-Host "错误：$($_.Exception.Message)" -ForegroundColor Red
    if (-not $DryRun) {
        Show-Rollback
    }
    exit 1
}
