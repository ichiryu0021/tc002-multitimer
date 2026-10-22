#!/usr/bin/env python3
"""Assertions over an Edge/Chrome --dump-dom snapshot.

The inline script source is deliberately excluded from the visible-text error
scan: a JS identifier such as ReferenceError in source code is not a runtime
error. Runtime console/page errors are checked separately over CDP.

Usage:
  python inspect_dom.py --file dom-mobile.html --out dom-report.json
"""

from __future__ import annotations

import argparse
import json
import re
from html.parser import HTMLParser
from pathlib import Path
from typing import Any


class SnapshotParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.ids: dict[str, str] = {}
        self.class_counts: dict[str, int] = {}
        self.inputs: list[dict[str, str]] = []
        self.buttons: list[dict[str, str]] = []
        self.canvases: list[dict[str, str]] = []
        self._in_script = False
        self._in_style = False
        self.visible_text: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        a = {k: (v or "") for k, v in attrs}
        if tag == "script":
            self._in_script = True
            return
        if tag == "style":
            self._in_style = True
            return
        if "id" in a:
            self.ids[a["id"]] = tag
        for cls in a.get("class", "").split():
            self.class_counts[cls] = self.class_counts.get(cls, 0) + 1
        if tag == "input":
            self.inputs.append(a)
        elif tag == "button":
            self.buttons.append(a)
        elif tag == "canvas":
            self.canvases.append(a)

    def handle_endtag(self, tag: str) -> None:
        if tag == "script":
            self._in_script = False
        elif tag == "style":
            self._in_style = False

    def handle_data(self, data: str) -> None:
        if not self._in_script and not self._in_style:
            text = data.strip()
            if text:
                self.visible_text.append(text)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--file", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=None)
    args = ap.parse_args()

    html = args.file.read_text(encoding="utf-8")
    parser = SnapshotParser()
    parser.feed(html)
    text = " ".join(parser.visible_text)
    text = re.sub(r"\s+", " ", text)
    status_items = re.findall(
        r'<div class="status-item ([^"]*)"><div class="head">'
        r'<span class="dot"></span><span class="t">(.*?)</span>'
        r'<span class="ts">(.*?)</span>',
        html,
        re.S,
    )
    device_lines = re.findall(
        r'<div class="dline"><span class="k">(.*?)</span>'
        r'<span class="v">(.*?)</span></div>',
        html,
    )
    save_button = next((b for b in parser.buttons if b.get("id") == "saveBtn"), None)
    save_text_match = re.search(r'<button id="saveBtn"[^>]*>(.*?)</button>', html, re.S)

    report: dict[str, Any] = {
        "file": str(args.file),
        "bytes": args.file.stat().st_size,
        "checks": {
            "led_canvas": any(c.get("id") == "led" for c in parser.canvases),
            "point_list": "pointList" in parser.ids,
            "point_rows": parser.class_counts.get("point-row", 0),
            "save_button": save_button is not None,
            "save_button_text": (
                re.sub(r"<[^>]+>", "", save_text_match.group(1)).strip()
                if save_text_match
                else None
            ),
            "device_info": "deviceInfo" in parser.ids,
            "device_info_shown": bool(
                re.search(r'id="deviceInfo"[^>]*style="display: block;', html)
            ),
            "device_info_lines": [" ".join(pair) for pair in device_lines],
            "status_items": [
                {"kind": kind, "title": title.strip(), "time": ts.strip()}
                for kind, title, ts in status_items
            ],
            "loaded_status_present": "已加载时钟配置" in text,
            "wifi_button_present": "wifiSaveBtn" in parser.ids,
            "time_sync_button_present": "timeSyncBtn" in parser.ids,
            "uncaught_error_text_in_visible_dom": bool(
                re.search(r"Uncaught|ReferenceError|TypeError|SyntaxError|未捕获异常", text)
            ),
        },
    }
    # The value property of a JS-populated time input is absent from outerHTML,
    # so point inputs are represented by their attributes and count only.
    report["checks"]["time_inputs"] = [
        {"id": i.get("id"), "type": i.get("type"), "value_attr": i.get("value")}
        for i in parser.inputs
        if i.get("type") == "time"
    ]
    report["visible_text_excerpt"] = text[:500]
    report["ok"] = all(
        [
            report["checks"]["led_canvas"],
            report["checks"]["point_list"],
            report["checks"]["point_rows"] == 2,
            report["checks"]["save_button"],
            report["checks"]["device_info_shown"],
            report["checks"]["loaded_status_present"],
            not report["checks"]["uncaught_error_text_in_visible_dom"],
        ]
    )

    out = json.dumps(report, ensure_ascii=False, indent=2)
    if args.out:
        args.out.write_text(out + "\n", encoding="utf-8")
    print(out)


if __name__ == "__main__":
    main()
