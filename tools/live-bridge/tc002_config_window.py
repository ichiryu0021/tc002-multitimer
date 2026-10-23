#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""TC002 完整配置页窗口（pywebview + WebView2）。

由主程序以子进程方式启动：TC002Desk.exe --config-window http://<设备IP>:8080/
复用设备自带的单文件配置页（页面/倒计时目标/直播房间/WiFi/NTP/模拟器）。
"""

from __future__ import annotations

import os
import sys
from urllib.parse import urlparse


def run_config_window(url: str) -> int:
    parsed = urlparse(url)
    if parsed.scheme != "http" or not parsed.hostname:
        sys.stderr.write("invalid config url: %s\n" % url)
        return 2
    try:
        import webview
    except Exception as exc:  # WebView2/pywebview 不可用时由主程序回退到浏览器
        sys.stderr.write("pywebview unavailable: %r\n" % (exc,))
        return 2

    storage_root = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
    storage_path = os.path.join(storage_root, "TC002Desk", "webview")
    try:
        os.makedirs(storage_path, exist_ok=True)
    except OSError:
        storage_path = None

    title = "TC002 配置 - %s" % parsed.hostname
    webview.create_window(
        title,
        url,
        width=1180,
        height=820,
        min_size=(900, 620),
        text_select=True,
    )
    try:
        webview.start(private_mode=False, storage_path=storage_path)
    except Exception as exc:
        sys.stderr.write("webview failed: %r\n" % (exc,))
        return 2
    return 0


if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.100.1:8080/"
    sys.exit(run_config_window(target))
