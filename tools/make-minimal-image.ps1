<#
.SYNOPSIS
    构建“最小 /res 镜像”：只含本项目应用、main.ftu、EasyUI.cfg、OpenSSL、公共 CA。
    不包含 Ulanzi 官方 stock /res 的字体、图标、音频、BlueZ、网页、测试 UI 等素材。
.DESCRIPTION
    输出 update.img + sha256 + build-info。应用从 /res/lib/libzkgui.so 启动；
    其余系统库（libeasyui/libzkhardware/libzknet 等）由官方固件 /lib 提供。
#>
param(
    [string]$SoPath = "build/v3g-workspace/MultiTimer/Release/libzkgui.so",
    [string]$OutDir = "dist-minimal",
    [string]$WorkDir = "build/flash/minimal-work",
    [string]$ToolsDir = "build/ide-tools"
)
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
function Abs([string]$p) {
    if ([System.IO.Path]::IsPathRooted($p)) { return $p }
    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $p))
}
function Sha([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }
$SoAbs = Abs $SoPath
$OutAbs = Abs $OutDir
$WorkAbs = Abs $WorkDir
$ToolsAbs = Abs $ToolsDir
$ResMin = Abs "native/MultiTimer/res-minimal"
$ExtraLib = Abs "native/MultiTimer/res-extra/lib"
$Mkimg = Join-Path $ToolsAbs "zkswe_mkimg.exe"
$Fsimg = Join-Path $ToolsAbs "fsimg.exe"
foreach ($f in @($SoAbs, $Mkimg, $Fsimg)) {
    if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { throw "缺少文件：$f" }
}
foreach ($f in @("$ResMin\etc\EasyUI.cfg", "$ResMin\ui\main.ftu", "$ResMin\ui\cacert.pem",
        "$ExtraLib\libssl.so.1.1", "$ExtraLib\libcrypto.so.1.1")) {
    if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { throw "缺少最小资源：$f" }
}
$cfgText = [System.IO.File]::ReadAllText((Join-Path $ResMin "etc\EasyUI.cfg"), [System.Text.Encoding]::UTF8)
if ($cfgText -notmatch '"startupLibPath"\s*:\s*"/res/lib/libzkgui.so"' -or
    $cfgText -notmatch '"resPath"\s*:\s*"/res/ui/"') {
    throw "res-minimal/etc/EasyUI.cfg 必须指向 /res/lib/libzkgui.so 和 /res/ui/"
}
if (Test-Path -LiteralPath $WorkAbs) { Remove-Item -LiteralPath $WorkAbs -Recurse -Force }
New-Item -ItemType Directory -Force -Path $WorkAbs, (Join-Path $WorkAbs "imgout\lib"), (Join-Path $WorkAbs "imgout\ui") | Out-Null
$ImgOut = Join-Path $WorkAbs "imgout"
Get-ChildItem -LiteralPath $ResMin -Force | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $ImgOut -Recurse -Force }
Copy-Item -LiteralPath $SoAbs -Destination (Join-Path $ImgOut "lib\libzkgui.so") -Force
Copy-Item -LiteralPath (Join-Path $ExtraLib "libssl.so.1.1") -Destination (Join-Path $ImgOut "lib\libssl.so.1.1") -Force
Copy-Item -LiteralPath (Join-Path $ExtraLib "libcrypto.so.1.1") -Destination (Join-Path $ImgOut "lib\libcrypto.so.1.1") -Force
$files = @(Get-ChildItem -LiteralPath $ImgOut -Recurse -File -Force)
Write-Host "== 最小 /res 镜像 ==" -ForegroundColor Cyan
Write-Host ("文件数: {0}" -f $files.Count)
$files | ForEach-Object { "  {0}  {1}" -f $_.Length, $_.FullName.Substring($ImgOut.Length + 1) }
$ResTmp = Join-Path $WorkAbs "res.tmp"
$UpdateTmp = Join-Path $WorkAbs "update.img"
Push-Location $WorkAbs
try {
    & $Mkimg "imgout" "res.tmp" "-noappend" "-force-uid" "1000" "-force-gid" "1000" "-comp" "xz"
    if ($LASTEXITCODE -ne 0) { throw "zkswe_mkimg failed: $LASTEXITCODE" }
    & $Fsimg "-i" "res:res.tmp" "-o" "update.img" "-p" "0x000606"
    if ($LASTEXITCODE -ne 0) { throw "fsimg failed: $LASTEXITCODE" }
} finally { Pop-Location }
New-Item -ItemType Directory -Force -Path $OutAbs | Out-Null
Copy-Item -LiteralPath $UpdateTmp -Destination (Join-Path $OutAbs "update.img") -Force
$Image = Join-Path $OutAbs "update.img"
$Hash = Sha $Image
[System.IO.File]::WriteAllText((Join-Path $OutAbs "update.img.sha256"), ("{0}  update.img`n" -f $Hash), (New-Object System.Text.UTF8Encoding($false)))
$info = @(
    "TC002 / Z21 minimal /res update image",
    "generated_local=$([DateTimeOffset]::Now.ToString('o'))",
    "platform=0x000606",
    "partition=res (mtd3, 8 MiB)",
    "source_res=$ResMin",
    "source_so=$SoAbs",
    "source_so_bytes=$((Get-Item -LiteralPath $SoAbs).Length)",
    "source_so_sha256=$(Sha $SoAbs)",
    "res_tmp=$ResTmp",
    "res_tmp_bytes=$((Get-Item -LiteralPath $ResTmp).Length)",
    "res_tmp_sha256=$(Sha $ResTmp)",
    "update_img=$Image",
    "update_img_bytes=$((Get-Item -LiteralPath $Image).Length)",
    "update_img_sha256=$Hash",
    "included=libzkgui.so, libssl.so.1.1, libcrypto.so.1.1, ui/main.ftu, ui/cacert.pem(public CA), etc/EasyUI.cfg",
    "excluded=Ulanzi stock /res assets (fonts/icons/audio/BlueZ/web/test UI)"
)
[System.IO.File]::WriteAllText((Join-Path $OutAbs "build-info-minimal.txt"), (($info -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))
Write-Host ("镜像: {0}  {1} 字节  SHA256 {2}" -f $Image, (Get-Item -LiteralPath $Image).Length, $Hash) -ForegroundColor Green
