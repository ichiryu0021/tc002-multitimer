#!/usr/bin/env python3
"""Temporary mock Ulanzi TC002 HTTP device for the web UI visual check.

Python standard library only. Serves the unmodified single-file page from
native/MultiTimer/web-src/index.html plus the two API endpoints used by it.

Usage:
  python mock_device.py --port 18080 --fixture exact \
      --log tests/visual/mock_requests_exact.jsonl

Fixtures:
  exact     - the canonical fixture requested by the visual-check task.
  relative  - two time points around the current UTC+8 time so the simulator
              shows a live countdown. Used only for the extra countdown shot.
"""

from __future__ import annotations

import argparse
import json
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INDEX = ROOT / "native" / "MultiTimer" / "web-src" / "index.html"
TZ_OFFSET = 8
EXACT_POINTS = [
    {"time": "10:00", "enabled": True},
    {"time": "12:30", "enabled": True},
]
COLORS = {
    "countdown": "#00FF00",
    "warning": "#FF2020",
    "countup": "#0080FF",
}

_log_lock = threading.Lock()


def _hhmm(seconds: int) -> str:
    seconds %= 86400
    return f"{seconds // 3600:02d}:{(seconds % 3600) // 60:02d}"


def make_config(fixture: str) -> dict[str, Any]:
    if fixture == "relative":
        now = time.time()
        local = time.gmtime(now + TZ_OFFSET * 3600)
        now_of_day = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec
        # Current time sits between the two points: the next point is 13 min
        # away, so the page's real-time-follow mode renders a green MM:SS
        # countdown. Keep a comfortable margin around the capture window.
        points = [
            {"time": _hhmm(now_of_day - 300), "enabled": True},
            {"time": _hhmm(now_of_day + 780), "enabled": True},
        ]
    else:
        points = [dict(p) for p in EXACT_POINTS]
    return {
        "tzOffset": TZ_OFFSET,
        "warnSeconds": 10,
        "colors": dict(COLORS),
        "points": points,
    }


class DeviceHTTPRequestHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "MockTC002/1.0"
    sys_version = ""

    def log_message(self, fmt: str, *args: Any) -> None:  # noqa: A003
        # Keep the browser/XMLHttpRequest noise out; structured log only.
        return

    @property
    def state(self) -> "MockDevice":
        return self.server.device  # type: ignore[attr-defined]

    def _record(
        self,
        method: str,
        status: int,
        request_body: Any = None,
        note: str = "",
    ) -> None:
        entry: dict[str, Any] = {
            "ts": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            "client": self.client_address[0],
            "method": method,
            "path": self.path,
            "status": status,
        }
        if request_body is not None:
            entry["body"] = request_body
        if note:
            entry["note"] = note
        line = json.dumps(entry, ensure_ascii=False)
        with _log_lock:
            self.state.log_path.parent.mkdir(parents=True, exist_ok=True)
            with self.state.log_path.open("a", encoding="utf-8") as fh:
                fh.write(line + "\n")
                fh.flush()
        print(line, flush=True)

    def _send(
        self,
        status: int,
        body: bytes,
        content_type: str,
        *,
        head_only: bool = False,
        extra_headers: dict[str, str] | None = None,
    ) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        if extra_headers:
            for key, value in extra_headers.items():
                self.send_header(key, value)
        self.end_headers()
        if not head_only:
            self.wfile.write(body)
        self.wfile.flush()

    def _serve_json(
        self, status: int, obj: Any, *, head_only: bool = False
    ) -> None:
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self._send(
            status,
            body,
            "application/json; charset=utf-8",
            head_only=head_only,
        )

    def do_GET(self) -> None:  # noqa: N802
        route = self.path.split("?", 1)[0]
        if route in ("/", "/index.html"):
            try:
                body = self.state.index_path.read_bytes()
            except OSError as exc:
                self._send(500, str(exc).encode("utf-8"), "text/plain; charset=utf-8")
                self._record("GET", 500, note=f"cannot read index: {exc}")
                return
            self._send(200, body, "text/html; charset=utf-8")
            self._record("GET", 200)
            return

        if route == "/api/config":
            config = make_config(self.state.fixture)
            now_epoch = int(time.time())
            payload = {
                "ok": True,
                "config": config,
                "device": {
                    "ip": "192.168.8.219",
                    "ssid": "TestWiFi",
                    "wifi": True,
                    "uptimeSec": 90061,
                    "timeUtc": now_epoch,
                },
            }
            self._serve_json(200, payload)
            self._record("GET", 200, note=f"fixture={self.state.fixture}")
            return

        if route == "/favicon.ico":
            self._send(204, b"", "image/x-icon")
            return

        self._serve_json(404, {"ok": False, "error": "not found"})
        self._record("GET", 404)

    def do_HEAD(self) -> None:  # noqa: N802
        route = self.path.split("?", 1)[0]
        if route in ("/", "/index.html"):
            try:
                body = self.state.index_path.read_bytes()
            except OSError as exc:
                self._send(500, str(exc).encode("utf-8"), "text/plain; charset=utf-8", head_only=True)
                self._record("HEAD", 500)
                return
            self._send(200, body, "text/html; charset=utf-8", head_only=True)
            self._record("HEAD", 200)
            return
        if route == "/api/config":
            config = make_config(self.state.fixture)
            payload = {
                "ok": True,
                "config": config,
                "device": {
                    "ip": "192.168.8.219",
                    "ssid": "TestWiFi",
                    "wifi": True,
                    "uptimeSec": 90061,
                    "timeUtc": int(time.time()),
                },
            }
            self._serve_json(200, payload, head_only=True)
            self._record("HEAD", 200)
            return
        self._send(404, b"", "application/json", head_only=True)
        self._record("HEAD", 404)

    def do_POST(self) -> None:  # noqa: N802
        route = self.path.split("?", 1)[0]
        try:
            length = int(self.headers.get("Content-Length") or "0")
        except ValueError:
            length = 0
        raw = self.rfile.read(length) if length else b""
        parsed: Any = None
        try:
            parsed = json.loads(raw.decode("utf-8")) if raw else None
        except (UnicodeDecodeError, json.JSONDecodeError):
            parsed = raw.decode("utf-8", errors="replace")

        if route == "/api/time":
            # The firmware accepts an epoch; the mock only needs to confirm it.
            self._serve_json(200, {"ok": True})
            self._record("POST", 200, parsed)
            return
        if route == "/api/config":
            self._serve_json(200, {"ok": True})
            self._record("POST", 200, parsed)
            return

        self._serve_json(404, {"ok": False, "error": "not found"})
        self._record("POST", 404, parsed)


class MockDevice(ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(
        self,
        server_address: tuple[str, int],
        index_path: Path,
        log_path: Path,
        fixture: str,
    ) -> None:
        super().__init__(server_address, DeviceHTTPRequestHandler)
        self.index_path = index_path
        self.log_path = log_path
        self.fixture = fixture
        # Convenience handle used by the request handler's `state` property.
        self.device = self


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--fixture", choices=("exact", "relative"), default="exact")
    parser.add_argument("--web-index", type=Path, default=DEFAULT_INDEX)
    parser.add_argument("--log", type=Path, default=None)
    args = parser.parse_args()

    log_path = args.log or (Path(__file__).resolve().parent / "mock_requests.jsonl")
    log_path = log_path.resolve()
    if not args.web_index.is_file():
        raise SystemExit(f"index.html not found: {args.web_index}")
    # Fresh log for each server invocation.
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text("", encoding="utf-8")

    server = MockDevice(
        (args.host, args.port),
        args.web_index.resolve(),
        log_path,
        args.fixture,
    )
    print(
        f"mock TC002 listening on http://{args.host}:{args.port} "
        f"fixture={args.fixture} index={args.web_index.resolve()} log={log_path}",
        flush=True,
    )
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        print("mock TC002 stopped", flush=True)


if __name__ == "__main__":
    main()
