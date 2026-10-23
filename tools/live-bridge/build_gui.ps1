# 打包 TC002 桌面助手为单文件 exe（需要 Python + PyInstaller + pywebview + playwright）
# 用法：powershell -ExecutionPolicy Bypass -File build_gui.ps1
$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

python -m PyInstaller `
  --noconfirm --clean --onefile --windowed `
  --name TC002Desk `
  --add-data "desktop_ui.html;." `
  --collect-all webview `
  --collect-all playwright `
  --hidden-import webview.platforms.edgechromium `
  --hidden-import playwright.sync_api `
  --distpath dist --workpath build `
  forwarder_gui.py

if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed with exit code $LASTEXITCODE" }
Write-Host ""
Write-Host "已生成: $PSScriptRoot\dist\TC002Desk.exe"
