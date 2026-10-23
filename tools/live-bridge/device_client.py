#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""TC002 设备 HTTP API 客户端（设备自身 8080 端口）。

只使用设备公开的局域网接口，不依赖云服务：
  GET  /api/status       设备/WiFi/时间/当前页
  GET  /api/config       v3 配置（页面、直播、颜色、NTP…）
  GET  /api/wifi         当前网络 + 已保存网络（不含密码）
  GET  /api/live/status  直播数据源健康与房间快照
  POST /api/time         校时
  POST /api/page         切页
  POST /api/config       保存配置（含 live 参数）
  POST /api/live/refresh 触发直播刷新
  POST /api/wifi/list    保存 WiFi 列表/密码
"""

from __future__ import annotations

import concurrent.futures
import time
from typing import Any, Callable, Dict, Iterable, List, Optional

import requests

DEFAULT_DEVICE_PORT = 8080
DEFAULT_AP_IP = "192.168.100.1"
SCAN_WORKERS = 64
SCAN_TIMEOUT = 0.35


class DeviceError(Exception):
    """设备请求失败；message 可直接展示给用户。"""


class DeviceClient:
    def __init__(self, host: str, port: int = DEFAULT_DEVICE_PORT, timeout: float = 4.0):
        host = host.strip()
        if not host:
            raise DeviceError("设备 IP 不能为空")
        self.host = host
        self.port = int(port)
        self.timeout = timeout
        self.base_url = "http://%s:%d" % (self.host, self.port)

    # ------------------------------------------------------------------ HTTP
    def _request(
        self, method: str, path: str, payload: Optional[Dict[str, Any]] = None
    ) -> Dict[str, Any]:
        url = self.base_url + path
        try:
            if method == "GET":
                resp = requests.get(url, timeout=self.timeout)
            else:
                resp = requests.post(url, json=payload or {}, timeout=self.timeout)
        except requests.Timeout:
            raise DeviceError("设备响应超时（%s）" % url)
        except requests.RequestException as exc:
            raise DeviceError("无法连接设备 %s：%s" % (self.base_url, exc))

        if resp.status_code != 200:
            detail = ""
            try:
                data = resp.json()
                detail = str(data.get("error") or "")
            except ValueError:
                detail = (resp.text or "").strip()[:120]
            raise DeviceError(
                "设备返回 HTTP %d%s" % (resp.status_code, ("：" + detail) if detail else "")
            )

        try:
            data = resp.json()
        except ValueError:
            raise DeviceError("设备返回的不是 JSON（可能不是 TC002 应用端口）")
        if not isinstance(data, dict):
            raise DeviceError("设备返回的数据结构异常")
        if data.get("ok") is False:
            raise DeviceError("设备返回失败：%s" % (data.get("error") or "unknown"))
        return data

    # --------------------------------------------------------------- queries
    def status(self) -> Dict[str, Any]:
        return self._request("GET", "/api/status")

    def config(self) -> Dict[str, Any]:
        data = self._request("GET", "/api/config")
        config = data.get("config")
        if not isinstance(config, dict):
            raise DeviceError("设备配置格式异常（缺少 config）")
        return config

    def save_config(self, config: Dict[str, Any]) -> Dict[str, Any]:
        return self._request("POST", "/api/config", {"config": config})

    def wifi(self) -> Dict[str, Any]:
        return self._request("GET", "/api/wifi")

    def live_status(self) -> Dict[str, Any]:
        return self._request("GET", "/api/live/status")

    # --------------------------------------------------------------- actions
    def set_time(self, epoch: Optional[int] = None) -> Dict[str, Any]:
        return self._request("POST", "/api/time", {"epoch": int(epoch or time.time())})

    def switch_page(self, index: int) -> Dict[str, Any]:
        return self._request("POST", "/api/page", {"index": int(index)})

    def refresh_live(self, page: Optional[int] = None) -> Dict[str, Any]:
        payload: Dict[str, Any] = {}
        if page is not None:
            payload["page"] = int(page)
        return self._request("POST", "/api/live/refresh", payload)

    def update_live(
        self, source: str, url: str, refresh_sec: int, timeout_ms: int
    ) -> Dict[str, Any]:
        """读取完整 v3 配置，只改 live 四项后整份写回。"""
        if source not in ("off", "bridge", "hybrid", "push"):
            raise DeviceError("不支持的直播数据源：%s" % source)
        url = url.strip()
        if source in ("bridge", "hybrid") and not url:
            raise DeviceError("bridge/hybrid 模式必须填写转发地址")
        if url:
            if not url.startswith("http://"):
                raise DeviceError("转发地址必须是 http:// 开头（设备不支持 https）")
            if len(url) > 160 or " " in url:
                raise DeviceError("转发地址不能含空格，长度不超过 160")
        refresh_sec = int(refresh_sec)
        timeout_ms = int(timeout_ms)
        if not 15 <= refresh_sec <= 300:
            raise DeviceError("刷新周期必须在 15–300 秒之间")
        if not 1000 <= timeout_ms <= 5000:
            raise DeviceError("超时必须在 1000–5000 毫秒之间")

        config = self.config()
        live = config.get("live")
        if not isinstance(live, dict):
            live = {}
            config["live"] = live
        live["source"] = source
        live["url"] = url
        live["refreshSec"] = refresh_sec
        live["timeoutMs"] = timeout_ms
        return self._request("POST", "/api/config", {"config": config})

    def wifi_networks(self) -> List[Dict[str, Any]]:
        data = self.wifi()
        networks = data.get("networks")
        if not isinstance(networks, list):
            return []
        return [item for item in networks if isinstance(item, dict) and item.get("ssid")]

    def save_wifi_network(self, ssid: str, password: str = "") -> Dict[str, Any]:
        """新增或更新一个网络；同 SSID 留空密码时设备会保留原密码。"""
        ssid = ssid.strip()
        if not ssid or len(ssid) > 32:
            raise DeviceError("WiFi 名称必须在 1–32 个字符之间")
        if len(password) > 64:
            raise DeviceError("WiFi 密码不能超过 64 个字符")

        networks = [
            {"ssid": item["ssid"], "enabled": bool(item.get("enabled", True))}
            for item in self.wifi_networks()
        ]
        found = False
        for item in networks:
            if item["ssid"] == ssid:
                item["enabled"] = True
                if password:
                    item["password"] = password
                found = True
                break
        if not found:
            entry: Dict[str, Any] = {"ssid": ssid, "enabled": True}
            if password:
                entry["password"] = password
            networks.insert(0, entry)
        if len(networks) > 8:
            raise DeviceError("设备最多保存 8 个 WiFi，请先在设备上删除不用的网络")
        return self._request("POST", "/api/wifi/list", {"networks": networks})

    def set_wifi_enabled(self, ssid: str, enabled: bool) -> Dict[str, Any]:
        networks = [
            {
                "ssid": item["ssid"],
                "enabled": bool(enabled) if item["ssid"] == ssid else bool(item.get("enabled", True)),
            }
            for item in self.wifi_networks()
        ]
        found = any(item["ssid"] == ssid for item in networks)
        if not found:
            raise DeviceError("设备上没有找到 WiFi：%s" % ssid)
        return self._request("POST", "/api/wifi/list", {"networks": networks})

    def save_wifi_states(self, states: Iterable[Dict[str, Any]]) -> Dict[str, Any]:
        """只提交 ssid/enabled，设备会保留已有密码。"""
        networks = []
        for item in states:
            ssid = str(item.get("ssid") or "").strip()
            if not ssid:
                continue
            networks.append({"ssid": ssid, "enabled": bool(item.get("enabled", True))})
        if not networks:
            raise DeviceError("没有可保存的 WiFi 网络")
        if len(networks) > 8:
            raise DeviceError("设备最多保存 8 个 WiFi")
        return self._request("POST", "/api/wifi/list", {"networks": networks})


    # ------------------------------------------------------------- discovery
    def probe(self) -> Optional[Dict[str, Any]]:
        """探测单台设备；不是 TC002 或不可达时返回 None。"""
        return probe_host(self.host, self.port)


def probe_host(host: str, port: int = DEFAULT_DEVICE_PORT, timeout: float = SCAN_TIMEOUT) -> Optional[Dict[str, Any]]:
    url = "http://%s:%d/api/status" % (host, port)
    try:
        resp = requests.get(url, timeout=(min(0.2, timeout), timeout))
        if resp.status_code != 200:
            return None
        data = resp.json()
    except (requests.RequestException, ValueError):
        return None
    if not isinstance(data, dict) or data.get("ok") is not True:
        return None
    device = data.get("device")
    if not isinstance(device, dict):
        return None
    page = device.get("page")
    if not isinstance(page, dict):
        state = data.get("state")
        page = state.get("page") if isinstance(state, dict) else {}
    if not isinstance(page, dict):
        page = {}
    return {
        "host": host,
        "ip": device.get("ip") or host,
        "ssid": device.get("ssid") or "",
        "apMode": bool(device.get("apMode")),
        "pageTitle": page.get("title") or "",
        "pageIndex": page.get("index"),
        "pageCount": page.get("count"),
        "localTime": device.get("localTime") or "",
    }


def candidate_hosts(local_ips: Iterable[str]) -> List[str]:
    """本机每个 IPv4 所在的 /24 网段 + SoftAP 默认地址。"""
    hosts: List[str] = []
    seen = set()

    def add(host: str) -> None:
        if host not in seen:
            seen.add(host)
            hosts.append(host)

    for ip in local_ips:
        parts = str(ip).split(".")
        if len(parts) != 4 or not all(part.isdigit() for part in parts):
            continue
        prefix = ".".join(parts[:3])
        for last in range(1, 255):
            add("%s.%d" % (prefix, last))
    add(DEFAULT_AP_IP)
    return hosts


def scan_devices(
    local_ips: Iterable[str],
    timeout: float = SCAN_TIMEOUT,
    workers: int = SCAN_WORKERS,
    progress: Optional[Callable[[int, int], None]] = None,
) -> List[Dict[str, Any]]:
    """并发扫描局域网 8080 端口，返回识别到的 TC002 列表。"""
    hosts = candidate_hosts(local_ips)
    found: Dict[str, Dict[str, Any]] = {}
    total = len(hosts)
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, workers)) as pool:
        futures = {pool.submit(probe_host, host, DEFAULT_DEVICE_PORT, timeout): host for host in hosts}
        for future in concurrent.futures.as_completed(futures):
            done += 1
            try:
                result = future.result()
            except Exception:
                result = None
            if result:
                key = result.get("ip") or result.get("host")
                found[key] = result
            if progress is not None and (done % 16 == 0 or done == total):
                try:
                    progress(done, total)
                except Exception:
                    pass
    return sorted(found.values(), key=lambda item: item.get("ip") or "")


def format_uptime(seconds: Any) -> str:
    try:
        seconds = int(seconds)
    except (TypeError, ValueError):
        return "—"
    days, rem = divmod(max(0, seconds), 86400)
    hours, rem = divmod(rem, 3600)
    minutes = rem // 60
    if days:
        return "%d 天 %d 小时" % (days, hours)
    if hours:
        return "%d 小时 %d 分" % (hours, minutes)
    return "%d 分钟" % max(1, minutes)
