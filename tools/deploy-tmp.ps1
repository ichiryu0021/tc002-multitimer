#requires -Version 5.1
<#
.SYNOPSIS
    把 MultiTimer 临时部署到 TC002 的 /tmp（断电/重启后恢复官方固件）。

.DESCRIPTION
    这是 FlyThings IDE "下载调试" 的等价手工流程：
      /tmp/EasyUI.cfg  -> startupLibPath=/tmp/lib/libzkgui.so, resPath=/tmp/ui/
      /tmp/lib/libzkgui.so
      /tmp/ui/main.ftu
    然后 setprop ctl.restart zkswe。不会写 /res，不会固化。

.EXAMPLE
    .\tools\deploy-tmp.ps1 -DeviceIp 192.168.31.124

.EXAMPLE
    .\tools\deploy-tmp.ps1 -DeviceIp 192.168.31.124 -Restore
#>
param(
    [Parameter(Mandatory = $true)][string]$DeviceIp,
    [string]$Dist = "dist",
    [string]$Adb = "tools\platform-tools\adb.exe",
    [switch]$Restore
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$DistAbs = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Dist))
$AdbAbs = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Adb))

if (-not (Test-Path -LiteralPath $AdbAbs -PathType Leaf)) {
    throw "未找到 adb：$AdbAbs"
}

$serial = "${DeviceIp}:5555"
Write-Host "== TC002 /tmp deploy =="
Write-Host "Device : $serial"
& $AdbAbs connect $serial | Out-Host
& $AdbAbs -s $serial wait-for-device

if ($Restore) {
    & $AdbAbs -s $serial shell "rm -rf /tmp/lib /tmp/ui /tmp/EasyUI.cfg"
    & $AdbAbs -s $serial shell "setprop ctl.restart zkswe"
    Write-Host "已移除 /tmp 部署并重启；设备应恢复官方固件。"
    exit 0
}

foreach ($f in @("$DistAbs\EasyUI.cfg", "$DistAbs\lib\libzkgui.so", "$DistAbs\ui\main.ftu")) {
    if (-not (Test-Path -LiteralPath $f -PathType Leaf)) {
        throw "缺少部署文件：$f（先运行 tools\build.ps1，或使用已打包的 dist/）"
    }
}
$cfgText = Get-Content -LiteralPath "$DistAbs\EasyUI.cfg" -Raw
if ($cfgText -notmatch '/tmp/lib/libzkgui\.so' -or $cfgText -notmatch '/tmp/ui/') {
    throw "dist\EasyUI.cfg 不是 /tmp 部署配置（startupLibPath 必须为 /tmp/lib/libzkgui.so，resPath 必须为 /tmp/ui/）"
}

& $AdbAbs -s $serial shell "mkdir -p /tmp/lib /tmp/ui"
# 注意：不能直接 adb push 覆盖正在被 zkgui mmap 的 libzkgui.so，
# 否则运行中的进程会立刻崩溃（表现为 HTTP 8080 消失、设备重启回官方固件）。
# 先推到 .new，再用 mv 原子替换路径；旧进程继续映射旧 inode，restart 后加载新文件。
& $AdbAbs -s $serial push "$DistAbs\lib\libzkgui.so" "/tmp/lib/libzkgui.so.new"
& $AdbAbs -s $serial push "$DistAbs\ui\main.ftu" "/tmp/ui/main.ftu"
& $AdbAbs -s $serial push "$DistAbs\EasyUI.cfg" "/tmp/EasyUI.cfg"
& $AdbAbs -s $serial shell "rm -f /tmp/lib/libzkgui.so; mv /tmp/lib/libzkgui.so.new /tmp/lib/libzkgui.so; sync; setprop ctl.restart zkswe"

Write-Host ""
Write-Host "已推送并重启 zkswe。请等待 3~5 秒，然后访问："
Write-Host "  http://${DeviceIp}:8080/"
Write-Host "查看日志： $AdbAbs -s $serial logcat -v time"
Write-Host "恢复官方： .\tools\deploy-tmp.ps1 -DeviceIp $DeviceIp -Restore"
exit 0
