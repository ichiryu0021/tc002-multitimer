#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""TC002 桌面助手 —— pywebview 的 Python API。

UI 是一个本地 HTML 单页（desktop_ui.html），通过 window.pywebview.api 调用这里：
  - 直播转发：启动/停止 bridge、状态、日志
  - 设备 setup：发现/连接、状态、网络时间、直播参数、WiFi
  - 原生配置：倒计时目标、页面列表、颜色、时区、NTP（不调用设备网页）
"""

from __future__ import annotations

import datetime as _dt
import importlib.util
import logging
import re
import socket
import struct
import sys
import threading
from collections import deque
from email.utils import parsedate_to_datetime
from typing import Any, Dict, List, Optional

import requests

import bridge
from device_client import DeviceClient, DeviceError, scan_devices

APP_VERSION = "1.1.0"
DEFAULT_FORWARD_PORT = 8081

PAGE_TYPES = ("clock", "clock_countdown", "countdown", "live", "datetime")
ROOM_PLATFORMS = ("bilibili", "douyin", "kuaishou")
PAGE_METRICS = ("online", "likes", "fans", "status", "title", "none")
ROOM_METRICS = ("", "online", "likes", "fans", "status", "title", "none")
RE_PAGE_ID = re.compile(r"^[a-z0-9_-]{1,8}$")
RE_ROOM_ID = re.compile(r"^[A-Za-z0-9_-]{0,40}$")
RE_NTP_SERVER = re.compile(r"^[A-Za-z0-9._-]{1,64}(:\d{1,5})?$")
RE_HEX_COLOR = re.compile(r"^#[0-9A-Fa-f]{6}$")

NTP_SERVERS = (
    "ntp.ntsc.ac.cn",          # 中国科学院国家授时中心（默认）
    "ntp1.ntsc.ac.cn",
    "ntp2.ntsc.ac.cn",
    "time1.cloud.tencent.com",
    "ntp.aliyun.com",
    "cn.pool.ntp.org",
    "time.cloudflare.com",
)
NTP_EPOCH_DELTA = 2208988800


class _MemoryLogHandler(logging.Handler):
    def __init__(self, api: "DesktopApi"):
        super().__init__(logging.INFO)
        self.api = api

    def emit(self, record: logging.LogRecord) -> None:
        try:
            line = self.format(record)
        except Exception:
            line = record.getMessage()
        self.api._append_log(line)


class DesktopApi:
    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._forwarder: Optional[bridge.Bridge] = None
        self._httpd: Optional[bridge.BridgeServer] = None
        self._serve_thread: Optional[threading.Thread] = None
        self._forward_port = DEFAULT_FORWARD_PORT

        self._device: Optional[DeviceClient] = None
        self._snapshot: Dict[str, Any] = {}
        self._connected = False

        self._logs: "deque[tuple[int, str]]" = deque(maxlen=2000)
        self._log_seq = 0
        self._log_lock = threading.Lock()

        self._install_logging()

    # ------------------------------------------------------------- logging
    def _install_logging(self) -> None:
        logger = logging.getLogger("live-bridge")
        logger.setLevel(logging.INFO)
        logger.propagate = False
        for handler in list(logger.handlers):
            logger.removeHandler(handler)
        formatter = logging.Formatter("%(asctime)s %(levelname)-7s %(message)s", "%H:%M:%S")
        memory = _MemoryLogHandler(self)
        memory.setFormatter(formatter)
        logger.addHandler(memory)
        if sys.stderr is not None:
            stream = logging.StreamHandler(sys.stderr)
            stream.setFormatter(formatter)
            logger.addHandler(stream)

    def _append_log(self, line: str) -> None:
        with self._log_lock:
            self._log_seq += 1
            self._logs.append((self._log_seq, line))

    # ------------------------------------------------------------ metadata
    def app_info(self) -> Dict[str, Any]:
        return {
            "ok": True,
            "version": APP_VERSION,
            "bridgeVersion": bridge.VERSION,
            "hasPlaywright": importlib.util.find_spec("playwright") is not None,
        }

    # ----------------------------------------------------------- clipboard
    def copy_text(self, text: str) -> Dict[str, Any]:
        try:
            _copy_to_clipboard(str(text))
            return {"ok": True}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}

    # ------------------------------------------------------------ forwarder
    def forwarder_state(self) -> Dict[str, Any]:
        with self._lock:
            running = self._httpd is not None
            port = self._forward_port
            ips = bridge.local_ipv4_addresses()
            health = None
            if running and self._forwarder is not None:
                try:
                    health = self._forwarder.health()
                except Exception:
                    health = None
            platforms = (health or {}).get("platforms") or {}
            addresses = ["http://%s:%d/live" % (ip, port) for ip in ips]
            url = addresses[0] if addresses else ""
            if running and not url:
                url = "http://<本机IP>:%d/live" % port
            return {
                "ok": True,
                "running": running,
                "port": port,
                "addresses": addresses,
                "url": url if running else "",
                "bestUrl": self._best_bridge_url() or "",
                "health": health,
                "platforms": platforms,
                "hasPlaywright": importlib.util.find_spec("playwright") is not None,
            }

    def start_forwarder(self, port: Any = DEFAULT_FORWARD_PORT) -> Dict[str, Any]:
        try:
            port = int(port)
        except (TypeError, ValueError):
            return {"ok": False, "error": "端口必须是整数"}
        if not 1 <= port <= 65535:
            return {"ok": False, "error": "端口必须在 1–65535 之间"}
        with self._lock:
            if self._httpd is not None:
                return self.forwarder_state()
            cfg = bridge.Config(host="0.0.0.0", port=port)
            forwarder = bridge.Bridge(cfg)
            try:
                httpd = bridge.BridgeServer(("0.0.0.0", port), bridge.BridgeHandler)
            except OSError as exc:
                forwarder.close()
                return {"ok": False, "error": "端口 %d 无法监听：%s" % (port, exc)}
            httpd.bridge = forwarder
            self._forwarder = forwarder
            self._httpd = httpd
            self._forward_port = port
            self._serve_thread = threading.Thread(
                target=self._serve_forever, args=(httpd,), name="bridge-http", daemon=True
            )
            self._serve_thread.start()
            logging.getLogger("live-bridge").info("转发服务已启动：端口 %d" % port)
        return self.forwarder_state()

    def _serve_forever(self, httpd: bridge.BridgeServer) -> None:
        try:
            httpd.serve_forever(poll_interval=0.5)
        except Exception:
            pass

    def stop_forwarder(self) -> Dict[str, Any]:
        with self._lock:
            httpd, forwarder = self._httpd, self._forwarder
            if httpd is None:
                return {"ok": True, "running": False}
            self._httpd = None
            self._forwarder = None
            self._serve_thread = None
        try:
            httpd.shutdown()
        except Exception:
            pass
        try:
            httpd.server_close()
        except Exception:
            pass
        if forwarder is not None:
            try:
                forwarder.close()
            except Exception:
                pass
        logging.getLogger("live-bridge").info("转发服务已停止")
        return {"ok": True, "running": False}

    def _best_bridge_url(self) -> Optional[str]:
        with self._lock:
            if self._httpd is None:
                return None
            port = self._forward_port
        ips = bridge.local_ipv4_addresses()
        if not ips:
            return None
        picked = ips[0]
        device = self._device
        if device is not None:
            prefix = ".".join(device.host.split(".")[:3]) + "."
            for ip in ips:
                if ip.startswith(prefix):
                    picked = ip
                    break
        return "http://%s:%d/live" % (picked, port)

    def best_bridge_url(self) -> Dict[str, Any]:
        url = self._best_bridge_url()
        if not url:
            return {"ok": False, "error": "转发服务尚未启动，或本机没有局域网 IPv4"}
        return {"ok": True, "url": url}

    def logs_since(self, seq: Any = 0) -> Dict[str, Any]:
        try:
            seq = int(seq)
        except (TypeError, ValueError):
            seq = 0
        with self._log_lock:
            lines = [line for number, line in self._logs if number > seq]
            latest = self._log_seq
        return {"ok": True, "seq": latest, "lines": lines}

    # ------------------------------------------------------------- discovery
    def scan_devices(self) -> Dict[str, Any]:
        local_ips = bridge.local_ipv4_addresses()
        logging.getLogger("live-bridge").info(
            "搜索局域网内的 TC002（本机地址：%s）…" % (", ".join(local_ips) or "未检测到")
        )
        try:
            devices = scan_devices(local_ips)
        except Exception as exc:
            return {"ok": False, "error": str(exc), "devices": []}
        logging.getLogger("live-bridge").info("发现 %d 台 TC002" % len(devices))
        return {"ok": True, "devices": devices}

    # --------------------------------------------------------------- device
    def connect_device(self, host: str, port: Any = 8080) -> Dict[str, Any]:
        try:
            port = int(port)
        except (TypeError, ValueError):
            return {"ok": False, "error": "设备端口必须是整数"}
        try:
            client = DeviceClient(host, port)
        except DeviceError as exc:
            return {"ok": False, "error": str(exc)}
        logging.getLogger("live-bridge").info("正在连接 TC002：%s" % client.base_url)
        with self._lock:
            try:
                snapshot = self._fetch_snapshot(client)
            except DeviceError as exc:
                self._connected = False
                return {"ok": False, "error": str(exc)}
            self._device = client
            self._snapshot = snapshot
            self._connected = True
        logging.getLogger("live-bridge").info("已连接 TC002：%s" % client.base_url)
        return {
            "ok": True,
            "host": client.host,
            "port": client.port,
            "snapshot": snapshot,
        }

    def _fetch_snapshot(self, client: DeviceClient) -> Dict[str, Any]:
        snapshot: Dict[str, Any] = {"status": client.status()}
        errors: Dict[str, str] = {}
        for key, fetch in (
            ("config", client.config),
            ("wifi", client.wifi),
            ("live", client.live_status),
        ):
            try:
                snapshot[key] = fetch()
            except DeviceError as exc:
                snapshot[key] = None
                errors[key] = str(exc)
        if errors:
            snapshot["errors"] = errors
        return snapshot

    def device_state(self) -> Dict[str, Any]:
        with self._lock:
            client = self._device
            if client is None:
                return {"ok": True, "connected": False, "hasDevice": False}
            try:
                snapshot = self._fetch_snapshot(client)
            except DeviceError as exc:
                self._connected = False
                return {
                    "ok": False,
                    "connected": False,
                    "hasDevice": True,
                    "host": client.host,
                    "port": client.port,
                    "error": str(exc),
                }
            self._snapshot = snapshot
            self._connected = True
            return {
                "ok": True,
                "connected": True,
                "hasDevice": True,
                "host": client.host,
                "port": client.port,
                "snapshot": snapshot,
            }

    def _require_device(self) -> DeviceClient:
        client = self._device
        if client is None:
            raise DeviceError("请先连接 TC002 设备")
        return client

    def refresh_live(self) -> Dict[str, Any]:
        try:
            client = self._require_device()
            page = self._current_page_index()
            result = client.refresh_live(page)
            return {"ok": True, "result": result}
        except DeviceError as exc:
            return {"ok": False, "error": str(exc)}

    def sync_network_time(self) -> Dict[str, Any]:
        """从网络时间源取 UTC，再写入设备；不使用电脑本地时钟。"""
        epoch: Optional[float] = None
        source = ""
        errors: List[str] = []
        for host in NTP_SERVERS:
            try:
                epoch = _query_ntp(host)
                source = "NTP " + host
                break
            except Exception as exc:
                errors.append("%s(%s)" % (host, type(exc).__name__))
        if epoch is None:
            for url in ("https://www.baidu.com", "https://www.taobao.com"):
                try:
                    epoch = _query_http_date(url)
                    source = "HTTP " + url
                    break
                except Exception as exc:
                    errors.append("%s(%s)" % (url, type(exc).__name__))
        if epoch is None or not 1700000000 <= epoch <= 4102444800:
            detail = "；".join(errors[-4:])
            return {"ok": False, "error": "网络时间获取失败" + ("：" + detail if detail else "")}
        try:
            client = self._require_device()
            result = client.set_time(int(epoch))
        except DeviceError as exc:
            return {"ok": False, "error": str(exc)}
        local = (result or {}).get("localTime") or ""
        logging.getLogger("live-bridge").info(
            "网络时间已同步：%s（来源 %s）" % (local or int(epoch), source)
        )
        return {
            "ok": True,
            "source": source,
            "epoch": int(epoch),
            "localTime": local,
            "result": result,
        }

    def switch_page(self, delta: Any = 1) -> Dict[str, Any]:
        try:
            delta = int(delta)
        except (TypeError, ValueError):
            return {"ok": False, "error": "翻页步长必须是整数"}
        try:
            client = self._require_device()
            page = ((self._snapshot.get("status") or {}).get("device") or {}).get("page") or {}
            index = page.get("index")
            count = page.get("count")
            if not isinstance(index, int) or not isinstance(count, int) or count <= 0:
                return {"ok": False, "error": "还没有读取到设备页面信息，请先刷新"}
            target = (index + delta) % count
            client.switch_page(target)
            return {"ok": True, "index": target, "count": count}
        except DeviceError as exc:
            return {"ok": False, "error": str(exc)}

    def push_live_config(
        self, source: str, url: str, refresh_sec: Any = 30, timeout_ms: Any = 3000
    ) -> Dict[str, Any]:
        try:
            client = self._require_device()
            result = client.update_live(source, url, int(refresh_sec), int(timeout_ms))
        except (DeviceError, TypeError, ValueError) as exc:
            return {"ok": False, "error": str(exc)}
        warning = None
        try:
            client.refresh_live(self._current_page_index())
        except DeviceError as exc:
            warning = "配置已保存，但触发刷新失败：%s" % exc
        return {"ok": True, "result": result, "warning": warning}

    def _current_page_index(self) -> Optional[int]:
        page = ((self._snapshot.get("status") or {}).get("device") or {}).get("page") or {}
        index = page.get("index")
        return index if isinstance(index, int) else None

    # ----------------------------------------------------------------- WiFi
    def add_wifi(self, ssid: str, password: str = "") -> Dict[str, Any]:
        try:
            client = self._require_device()
            result = client.save_wifi_network(ssid, password)
            return {"ok": True, "result": result}
        except DeviceError as exc:
            return {"ok": False, "error": str(exc)}

    def toggle_wifi(self, ssid: str, enabled: bool) -> Dict[str, Any]:
        try:
            client = self._require_device()
            result = client.set_wifi_enabled(ssid, bool(enabled))
            return {"ok": True, "result": result}
        except DeviceError as exc:
            return {"ok": False, "error": str(exc)}

    # -------------------------------------------------------------- targets
    def save_targets(self, targets: List[Dict[str, Any]]) -> Dict[str, Any]:
        if not isinstance(targets, list):
            return {"ok": False, "error": "targets 必须是数组"}
        if len(targets) > 12:
            return {"ok": False, "error": "最多 12 个倒计时目标"}
        cleaned: List[Dict[str, Any]] = []
        for index, item in enumerate(targets):
            if not isinstance(item, dict):
                return {"ok": False, "error": "第 %d 个目标格式错误" % (index + 1)}
            date_text = str(item.get("date") or "").strip()
            time_text = str(item.get("time") or "").strip()
            enabled = bool(item.get("enabled", True))
            try:
                _dt.date.fromisoformat(date_text)
            except ValueError:
                return {"ok": False, "error": "第 %d 个目标日期无效（YYYY-MM-DD）" % (index + 1)}
            try:
                parsed_time = _dt.time.fromisoformat(time_text)
            except ValueError:
                return {"ok": False, "error": "第 %d 个目标时间无效（HH:MM:SS）" % (index + 1)}
            cleaned.append(
                {
                    "date": date_text,
                    "time": parsed_time.strftime("%H:%M:%S"),
                    "enabled": enabled,
                }
            )
        try:
            client = self._require_device()
            config = client.config()
            config["targets"] = cleaned
            result = client.save_config(config)
            logging.getLogger("live-bridge").info("已保存 %d 个倒计时目标" % len(cleaned))
            return {"ok": True, "result": result, "count": len(cleaned)}
        except DeviceError as exc:
            return {"ok": False, "error": str(exc)}

    # ---------------------------------------------------------------- pages
    def save_pages(
        self, pages: List[Dict[str, Any]], default_page_id: str = ""
    ) -> Dict[str, Any]:
        if not isinstance(pages, list) or not 1 <= len(pages) <= 12:
            return {"ok": False, "error": "页面数量必须在 1–12 之间"}
        cleaned: List[Dict[str, Any]] = []
        seen = set()
        for index, item in enumerate(pages):
            if not isinstance(item, dict):
                return {"ok": False, "error": "第 %d 个页面格式错误" % (index + 1)}
            page_id = str(item.get("id") or "").strip()
            if not RE_PAGE_ID.match(page_id):
                return {"ok": False, "error": "第 %d 个页面 ID 无效（1–8 位小写字母/数字/_/-）" % (index + 1)}
            if page_id in seen:
                return {"ok": False, "error": "页面 ID 重复：%s" % page_id}
            page_type = str(item.get("type") or "").strip()
            if page_type not in PAGE_TYPES:
                return {"ok": False, "error": "第 %d 个页面类型无效：%s" % (index + 1, page_type)}
            title = str(item.get("title") or "").strip()[:16]
            short_title = str(item.get("shortTitle") or "").strip()
            if len(short_title) > 8:
                short_title = short_title[:8]
            if any(ord(ch) < 32 or ord(ch) > 126 for ch in short_title):
                return {"ok": False, "error": "第 %d 个页面短标题只能使用 ASCII" % (index + 1)}
            page: Dict[str, Any] = {
                "id": page_id,
                "type": page_type,
                "title": title or page_type,
                "shortTitle": short_title,
                "enabled": bool(item.get("enabled", True)),
            }
            if page_type == "live":
                metric = str(item.get("metric") or "online").strip()
                if metric not in PAGE_METRICS:
                    return {"ok": False, "error": "第 %d 个页面默认指标无效" % (index + 1)}
                rooms = item.get("rooms")
                if not isinstance(rooms, list) or not 1 <= len(rooms) <= 2:
                    return {"ok": False, "error": "第 %d 个直播页必须有 1–2 个房间" % (index + 1)}
                clean_rooms: List[Dict[str, Any]] = []
                for room_index, room in enumerate(rooms):
                    if not isinstance(room, dict):
                        return {"ok": False, "error": "第 %d 个页面第 %d 个房间格式错误" % (index + 1, room_index + 1)}
                    platform = str(room.get("platform") or "").strip()
                    if platform not in ROOM_PLATFORMS:
                        return {"ok": False, "error": "第 %d 个页面第 %d 个房间平台无效" % (index + 1, room_index + 1)}
                    room_id = str(room.get("room") or "").strip()
                    if not RE_ROOM_ID.match(room_id):
                        return {"ok": False, "error": "第 %d 个页面第 %d 个房间号只能包含字母/数字/_/-（≤40）" % (index + 1, room_index + 1)}
                    label = "".join(
                        ch for ch in str(room.get("label") or "").upper()
                        if ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"
                    )[:3]
                    room_metric = str(room.get("metric") or "").strip()
                    if room_metric not in ROOM_METRICS:
                        return {"ok": False, "error": "第 %d 个页面第 %d 个房间指标无效" % (index + 1, room_index + 1)}
                    clean_rooms.append(
                        {
                            "platform": platform,
                            "room": room_id,
                            "label": label,
                            "enabled": bool(room.get("enabled", True)),
                            "metric": room_metric,
                        }
                    )
                page["metric"] = metric
                page["rooms"] = clean_rooms
            seen.add(page_id)
            cleaned.append(page)
        default_id = str(default_page_id or "").strip()
        if default_id not in seen:
            default_id = cleaned[0]["id"]
        try:
            client = self._require_device()
            config = client.config()
            config["pages"] = cleaned
            config["defaultPageId"] = default_id
            result = client.save_config(config)
            logging.getLogger("live-bridge").info("已保存 %d 个页面，默认页 %s" % (len(cleaned), default_id))
            return {
                "ok": True,
                "result": result,
                "count": len(cleaned),
                "defaultPageId": default_id,
            }
        except DeviceError as exc:
            return {"ok": False, "error": str(exc)}

    # --------------------------------------------------------------- display
    def save_display(
        self,
        tz_offset_minutes: Any,
        warn_seconds: Any,
        ntp_server: str,
        colors: Dict[str, str],
    ) -> Dict[str, Any]:
        try:
            tz_offset = int(tz_offset_minutes)
            warn = int(warn_seconds)
        except (TypeError, ValueError):
            return {"ok": False, "error": "时区和预警秒数必须是整数"}
        if not -720 <= tz_offset <= 840:
            return {"ok": False, "error": "时区必须在 -720..840 分钟之间"}
        if not 0 <= warn <= 600:
            return {"ok": False, "error": "预警秒数必须在 0..600 之间"}
        ntp = str(ntp_server or "").strip()
        if ntp and not RE_NTP_SERVER.match(ntp):
            return {"ok": False, "error": "NTP 服务器格式无效（host 或 host:port，≤64）"}
        if not isinstance(colors, dict):
            colors = {}
        cleaned_colors: Dict[str, str] = {}
        for key in ("clock", "countdown", "countup", "warning", "dim"):
            if key in colors:
                value = str(colors[key]).upper()
                if not RE_HEX_COLOR.match(value):
                    return {"ok": False, "error": "颜色 %s 必须是 #RRGGBB" % key}
                cleaned_colors[key] = value
        try:
            client = self._require_device()
            config = client.config()
            config["tzOffsetMinutes"] = tz_offset
            config["warnSeconds"] = warn
            config["ntpServer"] = ntp
            existing = config.get("colors")
            if not isinstance(existing, dict):
                existing = {}
            existing.update(cleaned_colors)
            config["colors"] = existing
            result = client.save_config(config)
            logging.getLogger("live-bridge").info(
                "已保存显示设置：tz=%d warn=%d ntp=%s" % (tz_offset, warn, ntp or "(默认)")
            )
            return {"ok": True, "result": result}
        except DeviceError as exc:
            return {"ok": False, "error": str(exc)}

    # -------------------------------------------------------------- shutdown
    def shutdown(self) -> None:
        try:
            self.stop_forwarder()
        except Exception:
            pass


def _query_ntp(host: str, timeout: float = 1.5) -> float:
    """UDP NTP 查询，返回 UTC epoch。"""
    packet = b"\x1b" + b"\x00" * 47
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.sendto(packet, (host, 123))
        data, _address = sock.recvfrom(64)
    if len(data) < 48:
        raise RuntimeError("响应过短")
    if data[1] == 0:
        raise RuntimeError("NTP 拒绝服务")
    seconds = struct.unpack("!I", data[40:44])[0]
    fraction = struct.unpack("!I", data[44:48])[0]
    if seconds < NTP_EPOCH_DELTA:
        raise RuntimeError("NTP 时间无效")
    return seconds - NTP_EPOCH_DELTA + fraction / 4294967296.0


def _query_http_date(url: str, timeout: float = 3.0) -> float:
    """HTTP Date 头兜底（NTP 被防火墙拦截时）。"""
    response = requests.head(url, timeout=timeout, allow_redirects=True)
    response.raise_for_status()
    header = response.headers.get("Date")
    if not header:
        raise RuntimeError("缺少 Date 头")
    parsed = parsedate_to_datetime(header)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=_dt.timezone.utc)
    return parsed.timestamp()


def _copy_to_clipboard(text: str) -> None:
    if sys.platform != "win32":
        raise RuntimeError("当前平台不支持剪贴板写入")
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    cf_unicodetext = 13
    gmem_moveable = 0x0002

    kernel32.GlobalAlloc.argtypes = [wintypes.UINT, ctypes.c_size_t]
    kernel32.GlobalAlloc.restype = ctypes.c_void_p
    kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
    kernel32.GlobalLock.restype = ctypes.c_void_p
    kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]
    kernel32.GlobalUnlock.restype = wintypes.BOOL
    user32.OpenClipboard.argtypes = [ctypes.c_void_p]
    user32.OpenClipboard.restype = wintypes.BOOL
    user32.EmptyClipboard.restype = wintypes.BOOL
    user32.CloseClipboard.restype = wintypes.BOOL
    user32.SetClipboardData.argtypes = [wintypes.UINT, ctypes.c_void_p]
    user32.SetClipboardData.restype = ctypes.c_void_p

    data = str(text).encode("utf-16-le") + b"\x00\x00"
    if not user32.OpenClipboard(None):
        raise RuntimeError("无法打开剪贴板")
    try:
        user32.EmptyClipboard()
        handle = kernel32.GlobalAlloc(gmem_moveable, len(data))
        if not handle:
            raise RuntimeError("剪贴板内存分配失败")
        pointer = kernel32.GlobalLock(handle)
        if not pointer:
            raise RuntimeError("剪贴板内存锁定失败")
        ctypes.memmove(pointer, data, len(data))
        kernel32.GlobalUnlock(handle)
        if not user32.SetClipboardData(cf_unicodetext, handle):
            raise RuntimeError("剪贴板写入失败")
    finally:
        user32.CloseClipboard()
