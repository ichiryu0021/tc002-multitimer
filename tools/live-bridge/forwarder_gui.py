#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""TC002 桌面助手（Windows 单窗口版）。

pywebview（系统 WebView2）作为 UI 容器：
  - 侧栏：设备概览 / 直播转发 / 倒计时目标 / 完整配置
  - 完整配置页内嵌设备自带网页（页面、目标、直播房间、WiFi、NTP、模拟器）
  - 转发与设备 API 在 desktop_api.py，HTML 在 desktop_ui.html

打包：powershell -ExecutionPolicy Bypass -File build_gui.ps1
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import webview

from desktop_api import DesktopApi

APP_TITLE = "TC002 桌面助手"


def _enable_dpi_awareness() -> None:
    if sys.platform != "win32":
        return
    try:
        import ctypes

        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass


def _resource_path(name: str) -> str:
    base = getattr(sys, "_MEIPASS", None) or os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base, name)


def _storage_path() -> str:
    root = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
    path = os.path.join(root, "TC002Desk", "webview")
    try:
        os.makedirs(path, exist_ok=True)
    except OSError:
        path = ""
    return path


def main() -> int:
    _enable_dpi_awareness()
    html_path = _resource_path("desktop_ui.html")
    if not os.path.isfile(html_path):
        sys.stderr.write("缺少 UI 文件：%s\n" % html_path)
        return 1
    html_uri = Path(html_path).as_uri()

    api = DesktopApi()
    webview.create_window(
        APP_TITLE,
        url=html_uri,
        js_api=api,
        width=1240,
        height=900,
        min_size=(1000, 720),
        text_select=True,
        confirm_close=False,
    )
    try:
        webview.start(private_mode=False, storage_path=_storage_path() or None)
    finally:
        api.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
