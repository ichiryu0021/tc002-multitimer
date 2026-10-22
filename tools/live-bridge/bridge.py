#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""MultiTimer v3 live-data bridge (LAN relay for TC002).

Device  ->  bridge :  GET http://<pc-ip>:8081/live?rooms=bilibili:6,douyin:123456789012
bridge  ->  platform: public anonymous endpoints only.

No account, no login, no form submission, no cookies persisted to disk.
The only cookie ever used is an anonymous Douyin ``ttwid`` kept in RAM.

Protocol: docs/V3-CONTRACT.md section 5.  Deployment: docs/LIVE-BRIDGE.md.

Exit codes for ``--once``: 0 = all rooms ok, 2 = some room failed, 1 = usage/internal.
"""

import argparse
import concurrent.futures
import importlib.util
import json
import logging
import queue
import re
import socket
import sys
import threading
import time
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, List, Optional, Tuple
from urllib.parse import parse_qs, urlparse

try:
    import requests
except ImportError:  # pragma: no cover - dependency guard
    sys.stderr.write(
        "bridge.py requires the 'requests' package.\n"
        "  python -m pip install -r requirements.txt\n"
    )
    raise SystemExit(2)


VERSION = "3.0.0"
LOG = logging.getLogger("live-bridge")

# ---------------------------------------------------------------------------
# Protocol / safety limits (frozen by docs/V3-CONTRACT.md section 5)
# ---------------------------------------------------------------------------
MAX_ROOMS = 8                 # bridge accepts up to 8 rooms per request
MAX_ROOM_LEN = 40             # contract: config rooms[].room <= 40 chars
MAX_RESPONSE_BYTES = 4096     # contract: bridge response body <= 4096 B
MAX_QUERY_BYTES = 1024        # device request is <= 256 B; 1 KB is a safe cap
MAX_CACHE_ENTRIES = 64        # tiny LRU-ish cache, bounded memory
MAX_TITLE_CHARS = 48          # default title truncation (UTF-8, 3 B/char worst case)

PLATFORMS = ("bilibili", "douyin", "kuaishou")

# Per-room minimum interval between two upstream requests, in seconds.
# Values follow docs/RESEARCH-live-apis.md section 6.5.
MIN_INTERVAL = {
    "bilibili": 10.0,   # measured safe: 20 req / 10 s
    "douyin": 60.0,     # anonymous enter gets throttled after ~a dozen requests
    "kuaishou": 60.0,   # browser based; keep it slow
}

DEFAULT_CACHE_TTL = 20.0      # contract asks for a 10-30 s cache
DEFAULT_TIMEOUT = 4.0         # per upstream HTTP request (connect+read)
DEFAULT_DEADLINE = 2.5        # device timeoutMs is 3000, answer before that
DEFAULT_STALE_MAX = 120.0     # serve a <=120 s old good value while refreshing
DEFAULT_RATE_BLOCK = 180.0    # douyin global pause after a reject
DEFAULT_WORKERS = 8
DEFAULT_TITLE_MAX = MAX_TITLE_CHARS

UA_DESKTOP = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
)
UA_ANDROID = (
    "Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Mobile Safari/537.36"
)

BILI_H5_URL = "https://api.live.bilibili.com/xlive/web-room/v1/index/getH5InfoByRoom?room_id={room}"
BILI_INFO_URL = "https://api.live.bilibili.com/room/v1/Room/get_info?room_id={room}"

DY_REFLOW_URL = (
    "https://webcast.amemv.com/webcast/room/reflow/info/"
    "?type_id=0&live_id=1&room_id={room}&aid=1128&app_name=aweme&app_id=1128"
    "&device_platform=android&version_code=290100"
)
DY_ENTER_URL = (
    "https://live.douyin.com/webcast/room/web/enter/"
    "?aid=6383&app_name=douyin_web&live_id=1&device_platform=web&language=zh-CN"
    "&enter_from=web_live&cookie_enabled=true&screen_width=1920&screen_height=1080"
    "&browser_language=zh-CN&browser_platform=Win32&browser_name=Mozilla"
    "&browser_version=5.0&web_rid={room}"
)
DY_TTWID_URL = "https://live.douyin.com/"
KS_ROOM_URL = "https://live.kuaishou.com/u/{room}"

H_BILIBILI = {
    "User-Agent": UA_DESKTOP,
    "Accept": "application/json, text/plain, */*",
    "Accept-Language": "zh-CN,zh;q=0.9",
    "Referer": "https://live.bilibili.com/",
}
H_DOUYIN_WEB = {
    "User-Agent": UA_DESKTOP,
    "Accept": "application/json, text/plain, */*",
    "Accept-Language": "zh-CN,zh;q=0.9",
}
H_DOUYIN_APP = {
    "User-Agent": UA_ANDROID,
    "Accept": "application/json, text/plain, */*",
    "Accept-Language": "zh-CN,zh;q=0.9",
}

RE_BILI_ROOM = re.compile(r"^\d{1,12}$")
RE_DOUYIN_ROOM = re.compile(r"^\d{4,20}$")
RE_DOUYIN_ROOM_ID = re.compile(r"^\d{19}$")
RE_KS_PRINCIPAL = re.compile(r"^[A-Za-z0-9_-]{2,40}$")
RE_ROOM_CHARSET = re.compile(r"^[A-Za-z0-9_-]+$")

# JavaScript evaluated in the Kuaishou page (Playwright optional path).
KS_JS = r"""
() => {
  const out = {found: false};
  try {
    const st = window.__INITIAL_STATE__;
    if (st) {
      out.found = true;
      const lr = st.liveroom || {};
      out.hasLiveroom = !!st.liveroom;
      const pl = (lr.playList || [])[0] || {};
      out.isLiving = !!pl.isLiving;
      out.title = pl.title || (pl.author && pl.author.name) || '';
      const w = (pl.watchingCount !== undefined && pl.watchingCount !== null)
        ? pl.watchingCount : (pl.author && pl.author.watchingCount);
      if (w !== undefined && w !== null) out.watching = String(w);
      const lk = (pl.likeCount !== undefined && pl.likeCount !== null)
        ? pl.likeCount : (pl.author && pl.author.likeCount);
      if (lk !== undefined && lk !== null) out.likes = String(lk);
      if (pl.errorType && pl.errorType.type !== undefined) out.errorType = pl.errorType.type;
    }
  } catch (e) { out.evalError = String(e); }
  try {
    const txt = document.body ? document.body.innerText : '';
    if (out.watching === undefined) {
      const m = txt.match(/([0-9][0-9.,]*)\s*(万|亿)?\s*人在看/);
      if (m) { out.watching = m[1] + (m[2] || ''); out.fromDom = true; }
    }
    if (out.likes === undefined) {
      const m2 = txt.match(/([0-9][0-9.,]*)\s*(万|亿)?\s*个?赞/);
      if (m2) { out.likes = m2[1] + (m2[2] || ''); out.fromDom = true; }
    }
    if (!out.title && document.title) {
      out.title = String(document.title).replace(/\s*-\s*快手.*$/, '');
    }
  } catch (e) {}
  return out;
}
"""


# ---------------------------------------------------------------------------
# Errors
# ---------------------------------------------------------------------------
class FetchError(Exception):
    """A single room could not be fetched. ``code`` is a short slug for the JSON."""

    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


class BadRequest(Exception):
    """The bridge request itself is unusable (HTTP 400)."""

    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


# ---------------------------------------------------------------------------
# Small pure helpers
# ---------------------------------------------------------------------------
_COUNT_SUFFIX = {
    "\u4ebf": 100000000,  # 亿
    "\u4e07": 10000,      # 万
    "w": 10000,
    "W": 10000,
    "k": 1000,
    "K": 1000,
}


def _as_int(value: Any) -> Optional[int]:
    """Best-effort integer. ``0`` stays ``0`` (a valid metric), junk becomes None."""
    if value is None or isinstance(value, bool):
        return int(value) if isinstance(value, bool) else None
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(round(value))
    if isinstance(value, str):
        return parse_count(value)
    return None


def parse_count(text: Any) -> Optional[int]:
    """Parse "19", "1.2万", "10万+", "1,234", "9.9亿" into an int."""
    if text is None:
        return None
    if isinstance(text, bool):
        return None
    if isinstance(text, (int, float)):
        return int(round(text))
    s = str(text).strip()
    if not s:
        return None
    s = s.replace(",", "").replace("\uff0c", "").replace(" ", "").replace("+", "")
    mult = 1
    for suffix, factor in _COUNT_SUFFIX.items():
        if s.endswith(suffix):
            mult = factor
            s = s[: -len(suffix)]
            break
    if not s:
        return None
    try:
        return int(round(float(s) * mult))
    except ValueError:
        return None


def clean_title(value: Any, limit: int) -> Optional[str]:
    """Strip control chars, collapse whitespace and truncate to ``limit`` chars."""
    if value is None or limit <= 0:
        return None
    s = "".join(ch if (ch >= " " and ch != "\x7f") else " " for ch in str(value))
    s = " ".join(s.split())
    if not s:
        return None
    if len(s) > limit:
        s = s[:limit]
    return s


def split_key(key: str) -> Tuple[str, str]:
    platform, _, room = key.partition(":")
    return platform, room


def parse_rooms(spec: str) -> List[str]:
    """Parse ``platform:room,...`` into canonical deduplicated keys.

    Structural problems raise :class:`BadRequest` (HTTP 400). Unknown platforms
    are kept so they become a per-room ``unsupported_platform`` error instead of
    failing the whole page.
    """
    if spec is None or not spec.strip():
        raise BadRequest("missing_rooms")
    keys: List[str] = []
    seen = set()
    for item in spec.split(","):
        item = item.strip()
        if not item:
            continue
        platform, sep, room = item.partition(":")
        if not sep:
            raise BadRequest("bad_room_key")
        platform = platform.strip().lower()
        room = room.strip()
        if not platform or not room or len(room) > MAX_ROOM_LEN:
            raise BadRequest("bad_room_key")
        if not RE_ROOM_CHARSET.match(room):
            raise BadRequest("bad_room_key")
        key = platform + ":" + room
        if key in seen:
            continue
        if len(keys) >= MAX_ROOMS:
            raise BadRequest("too_many_rooms")
        seen.add(key)
        keys.append(key)
    if not keys:
        raise BadRequest("missing_rooms")
    return keys


# ---------------------------------------------------------------------------
# HTTP plumbing
# ---------------------------------------------------------------------------
_thread_local = threading.local()


def _session() -> "requests.Session":
    """One requests.Session per worker thread (sessions are not thread safe)."""
    sess = getattr(_thread_local, "session", None)
    if sess is None:
        sess = requests.Session()
        adapter = requests.adapters.HTTPAdapter(
            pool_connections=4, pool_maxsize=8, max_retries=0
        )
        sess.mount("https://", adapter)
        sess.mount("http://", adapter)
        _thread_local.session = sess
    return sess


def http_get(
    url: str,
    headers: Dict[str, str],
    timeout: float,
    max_bytes: int,
    retries: int = 0,
) -> Tuple[int, Any, bytes]:
    """GET with a hard body cap. Retries only network errors and 5xx.

    Returns ``(status, headers, body)``. Never raises for HTTP status codes;
    raises ``FetchError("timeout"|"network"|"too_large")``.
    """
    attempt = 0
    while True:
        try:
            with _session().get(
                url,
                headers=headers,
                timeout=(min(3.0, timeout), timeout),
                allow_redirects=True,
                stream=True,
            ) as resp:
                status = resp.status_code
                if status >= 500 and attempt < retries:
                    attempt += 1
                    time.sleep(0.6)
                    continue
                chunks = []
                total = 0
                for chunk in resp.iter_content(8192):
                    if not chunk:
                        continue
                    total += len(chunk)
                    if total > max_bytes:
                        raise FetchError("too_large")
                    chunks.append(chunk)
                return status, resp.headers, b"".join(chunks)
        except requests.Timeout:
            if attempt < retries:
                attempt += 1
                continue
            raise FetchError("timeout")
        except requests.RequestException:
            if attempt < retries:
                attempt += 1
                time.sleep(0.6)
                continue
            raise FetchError("network")


def http_head(
    url: str, headers: Dict[str, str], timeout: float
) -> Tuple[int, Any, List[str]]:
    """HEAD used only to obtain the anonymous Douyin ``ttwid`` cookie."""
    try:
        resp = _session().head(
            url,
            headers=headers,
            timeout=(min(3.0, timeout), timeout),
            allow_redirects=False,
        )
        try:
            raw = resp.raw.headers
            cookies = raw.getlist("Set-Cookie") if hasattr(raw, "getlist") else []
            return resp.status_code, resp.headers, list(cookies or [])
        finally:
            resp.close()
    except requests.Timeout:
        raise FetchError("timeout")
    except requests.RequestException:
        raise FetchError("network")


def decode_json(body: bytes) -> Any:
    try:
        return json.loads(body.decode("utf-8", "replace"))
    except ValueError:
        raise FetchError("parse")


# ---------------------------------------------------------------------------
# Bilibili (works anonymously; no cookie, no signature)
# ---------------------------------------------------------------------------
def _bilibili_code_error(code: Optional[int]) -> FetchError:
    if code in (1, 19002000, 19002001, -400):
        return FetchError("not_found")
    if code in (-352, -412):
        return FetchError("rate_limited")
    return FetchError("bili_code_%s" % code)


def parse_bilibili_h5(obj: Any) -> Dict[str, Any]:
    if not isinstance(obj, dict):
        raise FetchError("parse")
    code = _as_int(obj.get("code"))
    if code != 0:
        raise _bilibili_code_error(code)
    data = obj.get("data") or {}
    if not isinstance(data, dict):
        raise FetchError("parse")
    room_info = data.get("room_info") or {}
    if not isinstance(room_info, dict) or not room_info:
        raise FetchError("parse")
    online = _as_int(room_info.get("online"))
    if online is None:
        online = _as_int((data.get("watched_show") or {}).get("num"))
    likes = _as_int((data.get("like_info_v3") or {}).get("total_likes"))
    anchor = data.get("anchor_info") or {}
    relation = anchor.get("relation_info") or {}
    fans = _as_int(relation.get("attention"))
    live_raw = _as_int(room_info.get("live_status"))
    status = None
    if live_raw is not None:
        status = 1 if live_raw == 1 else 0  # only live_status==1 counts as live
    return {
        "online": online,
        "likes": likes,
        "fans": fans,
        "status": status,
        "title": clean_title(room_info.get("title"), 200),
    }


def parse_bilibili_room_info(obj: Any) -> Dict[str, Any]:
    if not isinstance(obj, dict):
        raise FetchError("parse")
    code = _as_int(obj.get("code"))
    if code != 0:
        raise _bilibili_code_error(code)
    data = obj.get("data")
    if not isinstance(data, dict) or "room_id" not in data:
        raise FetchError("parse")
    live_raw = _as_int(data.get("live_status"))
    status = None
    if live_raw is not None:
        status = 1 if live_raw == 1 else 0
    return {
        "online": _as_int(data.get("online")),
        "likes": None,  # this endpoint has no like counter
        "fans": _as_int(data.get("attention")),
        "status": status,
        "title": clean_title(data.get("title"), 200),
    }


# ---------------------------------------------------------------------------
# Douyin
# ---------------------------------------------------------------------------
def _douyin_metrics(room: Dict[str, Any]) -> Dict[str, Any]:
    stats = room.get("stats") or {}
    online = _as_int(room.get("user_count"))
    if online is None:
        online = parse_count(room.get("user_count_str"))
    if online is None:
        online = parse_count(stats.get("user_count_str"))
    if online == 0:
        alt = parse_count(room.get("user_count_str"))
        if alt is None:
            alt = parse_count(stats.get("user_count_str"))
        if alt:
            online = alt
    likes = _as_int(room.get("like_count"))
    owner = room.get("owner") or {}
    follow = owner.get("follow_info") or {}
    fans = _as_int(follow.get("follower_count"))
    title = clean_title(room.get("title"), 200)
    if not title:
        title = clean_title(owner.get("nickname"), 200)
    live_raw = _as_int(room.get("status"))
    status = None
    if live_raw is not None:
        status = 1 if live_raw == 2 else 0  # 2 = live, 4 = ended
    return {
        "online": online,
        "likes": likes,
        "fans": fans,
        "status": status,
        "title": title,
    }


def parse_douyin_reflow(obj: Any) -> Dict[str, Any]:
    if not isinstance(obj, dict):
        raise FetchError("parse")
    code = _as_int(obj.get("status_code"))
    if code != 0:
        if code == 10011:
            raise FetchError("invalid_room")
        raise FetchError("douyin_code_%s" % code)
    data = obj.get("data") or {}
    room = data.get("room") or {}
    if not isinstance(room, dict) or not room:
        raise FetchError("not_found")
    return _douyin_metrics(room)


def parse_douyin_enter(obj: Any) -> Dict[str, Any]:
    if not isinstance(obj, dict):
        raise FetchError("parse")
    code = _as_int(obj.get("status_code"))
    if code != 0:
        raise FetchError("douyin_code_%s" % code)
    data = obj.get("data") or {}
    items = data.get("data")
    if not isinstance(items, list) or not items:
        raise FetchError("not_found")
    room = items[0]
    if not isinstance(room, dict) or not room:
        raise FetchError("parse")
    return _douyin_metrics(room)


def douyin_rejected(status: int, body: bytes) -> bool:
    """200 + 0 bytes is Douyin's unified refusal (measured)."""
    return status in (403, 429) or (status == 200 and not body.strip())


# ---------------------------------------------------------------------------
# Kuaishou
# ---------------------------------------------------------------------------
def parse_kuaishou_state(raw: Any) -> Dict[str, Any]:
    """Map the object returned by KS_JS into contract metrics.

    Kept pure so the mapping is unit-testable without a browser.
    """
    if not isinstance(raw, dict) or not raw.get("found"):
        raise FetchError("kuaishou_no_data")
    if _as_int(raw.get("errorType")) == 2:
        raise FetchError("rate_limited")  # "请求过快，请稍后重试"
    live = bool(raw.get("isLiving"))
    online = parse_count(raw.get("watching"))
    likes = parse_count(raw.get("likes"))
    title = clean_title(raw.get("title"), 200)
    if not live and online is None and not raw.get("hasLiveroom"):
        raise FetchError("kuaishou_no_data")
    if live and online is None and likes is None and not title:
        raise FetchError("kuaishou_no_data")
    return {
        "online": online,
        "likes": likes,
        "fans": None,  # not exposed on the web room page
        "status": 1 if live else 0,
        "title": title,
    }


# ---------------------------------------------------------------------------
# Cache entry / configuration
# ---------------------------------------------------------------------------
@dataclass
class Entry:
    ok: bool
    data: Optional[Dict[str, Any]]
    error: Optional[str]
    attempt_at: float
    ok_at: Optional[float] = None


@dataclass
class Config:
    host: str = "0.0.0.0"
    port: int = 8081
    timeout: float = DEFAULT_TIMEOUT
    cache_ttl: float = DEFAULT_CACHE_TTL
    deadline: float = DEFAULT_DEADLINE
    stale_max: float = DEFAULT_STALE_MAX
    rate_block: float = DEFAULT_RATE_BLOCK
    workers: int = DEFAULT_WORKERS
    title_max: int = DEFAULT_TITLE_MAX


# ---------------------------------------------------------------------------
# Optional Kuaishou browser worker (Playwright, sync API is thread bound)
# ---------------------------------------------------------------------------
class KuaishouBrowser:
    """Owns Playwright on one dedicated thread and exposes a blocking fetch()."""

    def __init__(self) -> None:
        self._jobs: "queue.Queue" = queue.Queue()
        self._ready = threading.Event()
        self._fatal: Optional[str] = None
        self._thread = threading.Thread(
            target=self._run, name="kuaishou-browser", daemon=True
        )
        self._thread.start()

    # -- public -----------------------------------------------------------
    def fetch(self, principal: str) -> Dict[str, Any]:
        if not self._ready.wait(30.0):
            raise FetchError("kuaishou_browser_error")
        if self._fatal:
            raise FetchError(self._fatal)
        future: "concurrent.futures.Future" = concurrent.futures.Future()
        self._jobs.put((future, principal))
        try:
            return future.result(timeout=40.0)
        except concurrent.futures.TimeoutError:
            raise FetchError("timeout")
        except FetchError:
            raise
        except Exception:
            raise FetchError("kuaishou_browser_error")

    def close(self) -> None:
        self._jobs.put(None)

    # -- worker thread ----------------------------------------------------
    def _run(self) -> None:
        try:
            from playwright.sync_api import sync_playwright
        except Exception:
            self._fatal = "kuaishou_requires_browser"
            self._ready.set()
            self._fail_queued()
            return

        pw = None
        browser = None
        context = None
        page = None
        try:
            pw = sync_playwright().start()
            browser = pw.chromium.launch(
                headless=True,
                args=[
                    "--disable-gpu",
                    "--no-sandbox",
                    "--disable-dev-shm-usage",
                    "--mute-audio",
                ],
            )
            context = browser.new_context(
                user_agent=UA_DESKTOP,
                locale="zh-CN",
                viewport={"width": 1280, "height": 800},
            )
            page = context.new_page()
            self._ready.set()
            while True:
                job = self._jobs.get()
                if job is None:
                    break
                future, principal = job
                if future.cancelled() or future.done():
                    continue
                try:
                    result = self._grab(page, principal)
                    if not future.done():
                        future.set_result(result)
                except FetchError as exc:
                    if not future.done():
                        future.set_exception(exc)
                except Exception as exc:  # never leak a traceback into the JSON
                    LOG.warning(
                        "kuaishou browser error room=%s type=%s",
                        principal,
                        type(exc).__name__,
                    )
                    if not future.done():
                        future.set_exception(FetchError("kuaishou_browser_error"))
        except Exception as exc:
            self._fatal = "kuaishou_browser_missing"
            LOG.error(
                "kuaishou: cannot start headless Chromium (%s). "
                "Install it with: python -m playwright install chromium",
                type(exc).__name__,
            )
            self._ready.set()
            self._fail_queued()
        finally:
            for obj in (page, context, browser):
                try:
                    if obj is not None:
                        obj.close()
                except Exception:
                    pass
            try:
                if pw is not None:
                    pw.stop()
            except Exception:
                pass

    def _fail_queued(self) -> None:
        code = self._fatal or "kuaishou_browser_error"
        while True:
            try:
                job = self._jobs.get_nowait()
            except queue.Empty:
                return
            if job is None:
                continue
            future, _principal = job
            if not future.done():
                future.set_exception(FetchError(code))

    def _grab(self, page: Any, principal: str) -> Dict[str, Any]:
        page.goto(
            KS_ROOM_URL.format(room=principal),
            wait_until="domcontentloaded",
            timeout=15000,
        )
        try:
            page.wait_for_function("() => !!window.__INITIAL_STATE__", timeout=4000)
        except Exception:
            pass  # DOM fallback still runs inside KS_JS
        raw = page.evaluate(KS_JS)
        return parse_kuaishou_state(raw)


# ---------------------------------------------------------------------------
# Bridge: cache + fetch + render
# ---------------------------------------------------------------------------
class Bridge:
    def __init__(self, cfg: Config):
        self.cfg = cfg
        self._lock = threading.RLock()
        self._cache: Dict[str, Entry] = {}
        self._inflight: Dict[str, "concurrent.futures.Future"] = {}
        self._executor = concurrent.futures.ThreadPoolExecutor(
            max_workers=max(1, cfg.workers), thread_name_prefix="fetch"
        )
        self._started = time.monotonic()
        self._douyin_ttwid: Optional[str] = None
        self._douyin_ttwid_at = 0.0
        self._douyin_block_until = 0.0
        self._bili_down_until = 0.0
        self._bili_h5_fails = 0
        self._ks: Optional[KuaishouBrowser] = None
        self._ks_lock = threading.Lock()
        self.has_playwright = importlib.util.find_spec("playwright") is not None

    # -- lifecycle --------------------------------------------------------
    def close(self) -> None:
        try:
            self._executor.shutdown(wait=False)
        except Exception:
            pass
        with self._ks_lock:
            worker, self._ks = self._ks, None
        if worker is not None:
            worker.close()

    # -- collection -------------------------------------------------------
    def collect(
        self, keys: List[str], deadline: Optional[float] = None, wait_all: bool = False
    ) -> Dict[str, Entry]:
        """Resolve every key, reusing cache/in-flight work. Never raises."""
        now = time.monotonic()
        out: Dict[str, Entry] = {}
        pending: Dict[str, "concurrent.futures.Future"] = {}
        for key in keys:
            entry = self._cache_get(key)
            if entry is not None and self._is_fresh(key, entry, now):
                out[key] = entry
                continue
            pending[key] = self._submit(key)
        if not pending:
            return out
        timeout = 60.0 if wait_all else (deadline if deadline is not None else self.cfg.deadline)
        concurrent.futures.wait(list(pending.values()), timeout=timeout)
        for key, future in pending.items():
            if future.done() and not future.cancelled():
                try:
                    out[key] = future.result()
                    continue
                except FetchError as exc:
                    out[key] = self._error_entry(exc.code)
                    continue
                except Exception:
                    out[key] = self._error_entry("internal")
                    continue
            # Still running: answer with a recent good value if we have one.
            cached = self._cache_get(key)
            if (
                cached is not None
                and cached.ok
                and cached.ok_at is not None
                and (time.monotonic() - cached.ok_at) <= self.cfg.stale_max
            ):
                out[key] = cached
                LOG.debug("%s still in flight, serving cache age=%.0fs", key, time.monotonic() - cached.ok_at)
            else:
                out[key] = self._error_entry("timeout")
        return out

    def _effective_ttl(self, key: str) -> float:
        platform, _ = split_key(key)
        return max(self.cfg.cache_ttl, MIN_INTERVAL.get(platform, self.cfg.cache_ttl))

    def _is_fresh(self, key: str, entry: Entry, now: float) -> bool:
        return (now - entry.attempt_at) < self._effective_ttl(key)

    def _cache_get(self, key: str) -> Optional[Entry]:
        with self._lock:
            return self._cache.get(key)

    def _cache_put(self, key: str, entry: Entry) -> None:
        with self._lock:
            self._cache[key] = entry
            if len(self._cache) > MAX_CACHE_ENTRIES:
                oldest = min(self._cache, key=lambda k: self._cache[k].attempt_at)
                self._cache.pop(oldest, None)

    @staticmethod
    def _error_entry(code: str) -> Entry:
        now = time.monotonic()
        return Entry(ok=False, data=None, error=code, attempt_at=now)

    def _submit(self, key: str) -> "concurrent.futures.Future":
        with self._lock:
            future = self._inflight.get(key)
            if future is not None and not future.done():
                return future
            future = self._executor.submit(self._fetch_and_store, key)
            self._inflight[key] = future
            future.add_done_callback(lambda f, k=key: self._forget_inflight(k, f))
            return future

    def _forget_inflight(self, key: str, future: Any) -> None:
        with self._lock:  # RLock: callback may fire on the submitting thread
            if self._inflight.get(key) is future:
                self._inflight.pop(key, None)

    def _fetch_and_store(self, key: str) -> Entry:
        started = time.monotonic()
        try:
            data = self._fetch(key)
            now = time.monotonic()
            entry = Entry(ok=True, data=data, error=None, attempt_at=now, ok_at=now)
            LOG.debug(
                "fetch ok %s in %dms online=%s likes=%s fans=%s status=%s",
                key,
                (now - started) * 1000,
                data.get("online"),
                data.get("likes"),
                data.get("fans"),
                data.get("status"),
            )
        except FetchError as exc:
            entry = Entry(ok=False, data=None, error=exc.code, attempt_at=time.monotonic())
            LOG.info("fetch failed %s error=%s in %dms", key, exc.code, (time.monotonic() - started) * 1000)
        except Exception:
            entry = Entry(ok=False, data=None, error="internal", attempt_at=time.monotonic())
            LOG.exception("fetch crashed %s", key)
        self._cache_put(key, entry)
        return entry

    def _fetch(self, key: str) -> Dict[str, Any]:
        platform, room = split_key(key)
        if platform == "bilibili":
            return self._fetch_bilibili(room)
        if platform == "douyin":
            return self._fetch_douyin(room)
        if platform == "kuaishou":
            return self._fetch_kuaishou(room)
        raise FetchError("unsupported_platform")

    # -- Bilibili ---------------------------------------------------------
    def _fetch_bilibili(self, room: str) -> Dict[str, Any]:
        if not RE_BILI_ROOM.match(room):
            raise FetchError("invalid_room")
        with self._lock:
            h5_down = time.monotonic() < self._bili_down_until
        h5_error: Optional[str] = None
        if not h5_down:
            try:
                return self._bilibili_h5(room)
            except FetchError as exc:
                h5_error = exc.code
                self._bilibili_note_h5_failure(exc.code)
                LOG.info(
                    "bilibili h5 room=%s failed error=%s, trying get_info fallback",
                    room,
                    h5_error,
                )
        try:
            data = self._bilibili_room_info(room)
        except FetchError as exc:
            if h5_error in ("not_found", "invalid_room") and exc.code in (
                "network",
                "timeout",
                "http_412",
                "http_403",
            ):
                raise FetchError(h5_error)
            raise
        return data

    def _bilibili_h5(self, room: str) -> Dict[str, Any]:
        status, _headers, body = http_get(
            BILI_H5_URL.format(room=room), H_BILIBILI, self.cfg.timeout, 256 * 1024
        )
        if status != 200:
            raise FetchError("http_%d" % status)
        data = parse_bilibili_h5(decode_json(body))
        with self._lock:
            self._bili_h5_fails = 0
            self._bili_down_until = 0.0
        return data

    def _bilibili_room_info(self, room: str) -> Dict[str, Any]:
        status, _headers, body = http_get(
            BILI_INFO_URL.format(room=room), H_BILIBILI, self.cfg.timeout, 256 * 1024
        )
        if status != 200:
            raise FetchError("http_%d" % status)
        return parse_bilibili_room_info(decode_json(body))

    def _bilibili_note_h5_failure(self, code: str) -> None:
        # "not_found" is about the room, not about endpoint health.
        if code in ("not_found", "invalid_room"):
            return
        with self._lock:
            self._bili_h5_fails += 1
            if self._bili_h5_fails >= 2:
                self._bili_down_until = time.monotonic() + 600.0
                LOG.warning(
                    "bilibili h5 endpoint unhealthy (2 failures), "
                    "using get_info only for 10 minutes"
                )

    # -- Douyin -----------------------------------------------------------
    def _fetch_douyin(self, room: str) -> Dict[str, Any]:
        if not RE_DOUYIN_ROOM.match(room):
            raise FetchError("invalid_room")
        with self._lock:
            blocked_until = self._douyin_block_until
        if time.monotonic() < blocked_until:
            raise FetchError("rate_limited")
        if RE_DOUYIN_ROOM_ID.match(room):
            return self._douyin_reflow(room)
        return self._douyin_enter(room)

    def _douyin_note_rate_limited(self, room: str) -> None:
        with self._lock:
            self._douyin_block_until = time.monotonic() + self.cfg.rate_block
        LOG.warning(
            "douyin rate limited room=%s, pausing douyin for %ds",
            room,
            int(self.cfg.rate_block),
        )

    def _douyin_reflow(self, room: str) -> Dict[str, Any]:
        status, _headers, body = http_get(
            DY_REFLOW_URL.format(room=room), H_DOUYIN_APP, self.cfg.timeout, 512 * 1024
        )
        if douyin_rejected(status, body):
            self._douyin_note_rate_limited(room)
            raise FetchError("rate_limited")
        if status != 200:
            raise FetchError("http_%d" % status)
        return parse_douyin_reflow(decode_json(body))

    def _douyin_enter(self, room: str) -> Dict[str, Any]:
        ttwid = self._douyin_get_ttwid()
        headers = dict(H_DOUYIN_WEB)
        headers["Cookie"] = "ttwid=" + ttwid  # anonymous, memory only, never logged
        headers["Referer"] = "https://live.douyin.com/" + room
        status, _headers, body = http_get(
            DY_ENTER_URL.format(room=room), headers, self.cfg.timeout, 2 * 1024 * 1024
        )
        if douyin_rejected(status, body):
            self._douyin_note_rate_limited(room)
            raise FetchError("rate_limited")
        if status != 200:
            raise FetchError("http_%d" % status)
        return parse_douyin_enter(decode_json(body))

    def _douyin_get_ttwid(self) -> str:
        now = time.monotonic()
        with self._lock:
            if self._douyin_ttwid and now < self._douyin_ttwid_at + 7200:
                return self._douyin_ttwid
        _status, _headers, cookies = http_head(DY_TTWID_URL, H_DOUYIN_WEB, self.cfg.timeout)
        token = ""
        for cookie in cookies:
            if cookie.startswith("ttwid="):
                token = cookie[len("ttwid="):].split(";")[0].strip()
                break
        if not token:
            # Some proxies merge Set-Cookie headers; try the plain header too.
            joined = "; ".join(cookies)
            match = re.search(r"ttwid=([^;,\s]+)", joined)
            if match:
                token = match.group(1)
        if not token:
            raise FetchError("douyin_no_ttwid")
        with self._lock:
            self._douyin_ttwid = token
            self._douyin_ttwid_at = time.monotonic()
        LOG.info("douyin anonymous ttwid refreshed (memory only, %d bytes)", len(token))
        return token

    # -- Kuaishou ---------------------------------------------------------
    def _fetch_kuaishou(self, room: str) -> Dict[str, Any]:
        if not RE_KS_PRINCIPAL.match(room):
            raise FetchError("invalid_room")
        if not self.has_playwright:
            raise FetchError("kuaishou_requires_browser")
        worker = self._kuaishou_worker()
        return worker.fetch(room)

    def _kuaishou_worker(self) -> KuaishouBrowser:
        with self._ks_lock:
            if self._ks is None:
                LOG.info("starting headless Chromium for kuaishou (Playwright)")
                self._ks = KuaishouBrowser()
            return self._ks

    # -- rendering --------------------------------------------------------
    def render(
        self, keys: List[str], entries: Dict[str, Entry]
    ) -> Tuple[Dict[str, Any], bytes]:
        """Build the contract JSON, hard-capped at MAX_RESPONSE_BYTES."""
        ts = int(time.time())
        last_body = b""
        for cap in (self.cfg.title_max, 24, 0):
            rooms = []
            for key in keys:
                platform, room = split_key(key)
                entry = entries.get(key)
                item: Dict[str, Any] = {
                    "key": key,
                    "platform": platform,
                    "room": room,
                    "ok": bool(entry is not None and entry.ok and entry.data),
                }
                if item["ok"]:
                    data = entry.data or {}
                    for field in ("online", "likes", "fans", "status"):
                        value = data.get(field)
                        if isinstance(value, bool):
                            continue
                        if isinstance(value, int):
                            item[field] = value
                        elif isinstance(value, str) and value.isdigit():
                            item[field] = int(value)
                    title = clean_title(data.get("title"), cap)
                    if title:
                        item["title"] = title
                else:
                    item["error"] = (entry.error if entry is not None and entry.error else "error")
                rooms.append(item)
            payload = {"ok": True, "ts": ts, "rooms": rooms}
            body = json.dumps(
                payload, ensure_ascii=False, separators=(",", ":")
            ).encode("utf-8")
            last_body = body
            if len(body) <= MAX_RESPONSE_BYTES:
                if cap != self.cfg.title_max:
                    LOG.debug("response trimmed (title cap %d) to fit 4 KB", cap)
                return payload, body
        LOG.warning("response still oversized after trimming (%d B)", len(last_body))
        payload = {"ok": False, "ts": ts, "error": "response_too_large"}
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        return payload, body

    # -- diagnostics ------------------------------------------------------
    def health(self) -> Dict[str, Any]:
        with self._lock:
            cached = len(self._cache)
        return {
            "ok": True,
            "ts": int(time.time()),
            "version": VERSION,
            "uptimeSec": int(time.monotonic() - self._started),
            "maxRooms": MAX_ROOMS,
            "cachedRooms": cached,
            "platforms": {
                "bilibili": "ready",
                "douyin": "ready",
                "kuaishou": "ready" if self.has_playwright else "requires_playwright",
            },
        }


# ---------------------------------------------------------------------------
# HTTP server
# ---------------------------------------------------------------------------
class BridgeServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True
    request_queue_size = 16

    def __init__(self, address: Tuple[str, int], handler: Any):
        self.bridge: Optional[Bridge] = None
        super().__init__(address, handler)


class BridgeHandler(BaseHTTPRequestHandler):
    server_version = "MultiTimerLiveBridge/" + VERSION
    sys_version = ""
    protocol_version = "HTTP/1.1"

    def version_string(self) -> str:  # avoid a trailing space in the Server header
        return self.server_version

    # Keep stderr clean; we log one structured line per request ourselves.
    def log_message(self, fmt: str, *args: Any) -> None:  # noqa: D102
        LOG.debug("%s - %s", self.client_address[0], fmt % args)

    def log_error(self, fmt: str, *args: Any) -> None:  # noqa: D102
        LOG.warning("%s - %s", self.client_address[0], fmt % args)

    def do_GET(self) -> None:  # noqa: N802 (stdlib naming)
        started = time.monotonic()
        self._status_sent = 0
        try:
            parsed = urlparse(self.path)
            path = parsed.path or "/"
            if len(parsed.query) > MAX_QUERY_BYTES:
                self._send_json(414, {"ok": False, "error": "uri_too_long"})
            elif path in ("/live", "/live/"):
                self._handle_live(parsed.query)
            elif path in ("/health", "/health/"):
                self._send_json(200, self.server.bridge.health())
            elif path == "/":
                self._send_json(
                    200,
                    {
                        "ok": True,
                        "service": "multitimer-live-bridge",
                        "version": VERSION,
                        "endpoints": ["/live?rooms=bilibili:6,douyin:<id>", "/health"],
                    },
                )
            else:
                self._send_json(404, {"ok": False, "error": "not_found"})
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True
        except Exception:
            LOG.exception("unhandled error for path=%s", self.path.split("?")[0])
            try:
                self._send_json(500, {"ok": False, "error": "internal"})
            except Exception:
                self.close_connection = True
        finally:
            LOG.info(
                "%s %s -> %d %dms client=%s",
                self.command,
                self.path.split("?")[0],
                self._status_sent,
                int((time.monotonic() - started) * 1000),
                self.client_address[0],
            )

    def do_POST(self) -> None:  # noqa: N802
        started = time.monotonic()
        self._status_sent = 0
        self._send_json(405, {"ok": False, "error": "method_not_allowed"})
        LOG.info(
            "%s %s -> %d %dms client=%s",
            self.command,
            self.path.split("?")[0],
            self._status_sent,
            int((time.monotonic() - started) * 1000),
            self.client_address[0],
        )

    def _handle_live(self, query: str) -> None:
        params = parse_qs(query, keep_blank_values=True)
        spec = params.get("rooms", [""])[0]
        try:
            keys = parse_rooms(spec)
        except BadRequest as exc:
            self._send_json(400, {"ok": False, "error": exc.code})
            return
        bridge = self.server.bridge
        entries = bridge.collect(keys, deadline=bridge.cfg.deadline)
        payload, body = bridge.render(keys, entries)
        self._send_json(200, payload, body=body)

    def _send_json(self, status: int, obj: Dict[str, Any], body: Optional[bytes] = None) -> None:
        if body is None:
            body = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self._status_sent = status
        try:
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            if self.command != "HEAD" and body:
                self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            self.close_connection = True


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def setup_logging(level: str) -> None:
    logging.basicConfig(
        level=getattr(logging, level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)-7s %(message)s",
        datefmt="%H:%M:%S",
        stream=sys.stderr,
    )


def local_ipv4_addresses() -> List[str]:
    ips: List[str] = []
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.connect(("8.8.8.8", 80))  # no packet is sent for UDP connect
            ip = sock.getsockname()[0]
            if ip and not ip.startswith("127."):
                ips.append(ip)
        finally:
            sock.close()
    except OSError:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = str(info[4][0])
            if ip and not ip.startswith("127.") and ip not in ips:
                ips.append(ip)
    except OSError:
        pass
    return ips


def run_once(cfg: Config, spec: str, pretty: bool) -> int:
    bridge = Bridge(cfg)
    try:
        try:
            keys = parse_rooms(spec)
        except BadRequest as exc:
            sys.stdout.write(
                json.dumps({"ok": False, "error": exc.code}, separators=(",", ":")) + "\n"
            )
            return 2
        entries = bridge.collect(keys, wait_all=True)
        payload, body = bridge.render(keys, entries)
        if pretty:
            sys.stdout.write(json.dumps(payload, ensure_ascii=False, indent=2) + "\n")
        else:
            sys.stdout.write(body.decode("utf-8") + "\n")
        failed = 0
        for key in keys:
            entry = entries.get(key)
            if entry is not None and entry.ok:
                data = entry.data or {}
                sys.stderr.write(
                    "OK   %-28s online=%s likes=%s fans=%s status=%s title=%s\n"
                    % (
                        key,
                        data.get("online"),
                        data.get("likes"),
                        data.get("fans"),
                        data.get("status"),
                        (data.get("title") or "")[:32],
                    )
                )
            else:
                failed += 1
                sys.stderr.write(
                    "FAIL %-28s error=%s\n"
                    % (key, entry.error if entry is not None else "internal")
                )
        sys.stderr.write(
            "payload=%d bytes (limit %d), rooms=%d, failed=%d\n"
            % (len(body), MAX_RESPONSE_BYTES, len(keys), failed)
        )
        return 0 if failed == 0 else 2
    finally:
        bridge.close()


def run_server(cfg: Config) -> int:
    bridge = Bridge(cfg)
    try:
        httpd = BridgeServer((cfg.host, cfg.port), BridgeHandler)
    except OSError as exc:
        LOG.error("cannot bind %s:%d -> %s", cfg.host, cfg.port, exc)
        LOG.error(
            "port in use? Windows: netstat -ano | findstr :%d ; Linux: ss -ltnp | grep :%d",
            cfg.port,
            cfg.port,
        )
        bridge.close()
        return 1
    httpd.bridge = bridge
    LOG.info("MultiTimer live bridge %s listening on %s:%d", VERSION, cfg.host, cfg.port)
    LOG.info("cache ttl %.0fs (per-platform minimum: %s)", cfg.cache_ttl, MIN_INTERVAL)
    LOG.info("response deadline %.1fs, upstream timeout %.1fs", cfg.deadline, cfg.timeout)
    ips = local_ipv4_addresses()
    if ips:
        for ip in ips:
            LOG.info("device url: http://%s:%d/live?rooms=bilibili:6", ip, cfg.port)
    else:
        LOG.info("device url: http://<this-pc-ip>:%d/live?rooms=bilibili:6", cfg.port)
    if not bridge.has_playwright:
        LOG.info("kuaishou: Playwright not installed -> kuaishou rooms return kuaishou_requires_browser")
    try:
        httpd.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        LOG.info("interrupted, shutting down")
    finally:
        httpd.server_close()
        bridge.close()
    return 0


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="bridge.py",
        description="MultiTimer v3 LAN live-data bridge (TC002). "
        "GET /live?rooms=bilibili:6 -> contract JSON.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--host", default="0.0.0.0", help="bind address")
    parser.add_argument("--port", type=int, default=8081, help="TCP port")
    parser.add_argument(
        "--cache-ttl",
        type=float,
        default=DEFAULT_CACHE_TTL,
        help="cache seconds; effective per room is max(ttl, platform minimum)",
    )
    parser.add_argument(
        "--timeout", type=float, default=DEFAULT_TIMEOUT, help="upstream request timeout (s)"
    )
    parser.add_argument(
        "--deadline-ms",
        type=int,
        default=int(DEFAULT_DEADLINE * 1000),
        help="answer the device within this budget even if a fetch is still running",
    )
    parser.add_argument(
        "--stale-max",
        type=float,
        default=DEFAULT_STALE_MAX,
        help="serve a value at most this old while a refresh is in flight",
    )
    parser.add_argument(
        "--rate-block",
        type=float,
        default=DEFAULT_RATE_BLOCK,
        help="pause douyin for this long after a 200/0-byte reject",
    )
    parser.add_argument("--workers", type=int, default=DEFAULT_WORKERS, help="fetch threads")
    parser.add_argument(
        "--title-max",
        type=int,
        default=DEFAULT_TITLE_MAX,
        help="max title characters in the response (0 = no titles)",
    )
    parser.add_argument("--log-level", default="INFO", help="DEBUG/INFO/WARNING/ERROR")
    parser.add_argument(
        "--once",
        action="store_true",
        help="fetch once, print the contract JSON and exit (no server)",
    )
    parser.add_argument(
        "--rooms",
        default="bilibili:6",
        help="rooms for --once, e.g. bilibili:6,kuaishou:3xwnf96yetb5ewm",
    )
    parser.add_argument("--pretty", action="store_true", help="indent JSON in --once output")
    parser.add_argument("--version", action="version", version="%(prog)s " + VERSION)
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    args = build_arg_parser().parse_args(argv)
    setup_logging(args.log_level)

    cfg = Config(
        host=args.host,
        port=max(1, min(65535, args.port)),
        timeout=max(1.0, min(30.0, args.timeout)),
        cache_ttl=max(5.0, min(300.0, args.cache_ttl)),
        deadline=max(0.5, min(15.0, args.deadline_ms / 1000.0)),
        stale_max=max(0.0, min(3600.0, args.stale_max)),
        rate_block=max(0.0, min(3600.0, args.rate_block)),
        workers=max(1, min(32, args.workers)),
        title_max=max(0, min(200, args.title_max)),
    )
    if not 10.0 <= cfg.cache_ttl <= 30.0:
        LOG.warning(
            "cache ttl %.0fs is outside the recommended 10-30s range",
            cfg.cache_ttl,
        )
    if args.once:
        return run_once(cfg, args.rooms, args.pretty)
    return run_server(cfg)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
