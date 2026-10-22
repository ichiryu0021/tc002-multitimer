#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Offline unit tests for bridge.py (no network, no browser).

Run from this directory:

    python -m unittest -v test_bridge
"""

import json
import logging
import os
import sys
import time
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bridge  # noqa: E402

logging.disable(logging.CRITICAL)  # keep unittest output clean


def _entry(data=None, error=None):
    now = time.monotonic()
    if error is not None:
        return bridge.Entry(ok=False, data=None, error=error, attempt_at=now)
    return bridge.Entry(ok=True, data=data, error=None, attempt_at=now, ok_at=now)


class TestParseRooms(unittest.TestCase):
    def test_single(self):
        self.assertEqual(bridge.parse_rooms("bilibili:6"), ["bilibili:6"])

    def test_multiple_and_whitespace(self):
        spec = " bilibili:6 , douyin:611063790284 ,kuaishou:3xwnf96yetb5ewm "
        self.assertEqual(
            bridge.parse_rooms(spec),
            ["bilibili:6", "douyin:611063790284", "kuaishou:3xwnf96yetb5ewm"],
        )

    def test_deduplicates_case_insensitive_platform(self):
        self.assertEqual(bridge.parse_rooms("bilibili:6,BILIBILI:6"), ["bilibili:6"])

    def test_unknown_platform_is_kept_for_per_room_error(self):
        self.assertEqual(bridge.parse_rooms("weibo:1"), ["weibo:1"])

    def test_max_eight_rooms(self):
        spec = ",".join("bilibili:%d" % i for i in range(1, 9))
        self.assertEqual(len(bridge.parse_rooms(spec)), 8)
        with self.assertRaises(bridge.BadRequest) as ctx:
            bridge.parse_rooms(spec + ",bilibili:9")
        self.assertEqual(ctx.exception.code, "too_many_rooms")

    def test_empty(self):
        for spec in ("", " ", ",,,"):
            with self.assertRaises(bridge.BadRequest) as ctx:
                bridge.parse_rooms(spec)
            self.assertEqual(ctx.exception.code, "missing_rooms")

    def test_bad_keys(self):
        for spec in ("bilibili", "bilibili:", ":6", "bilibili:6;rm", "bilibili:" + "1" * 41):
            with self.assertRaises(bridge.BadRequest) as ctx:
                bridge.parse_rooms(spec)
            self.assertEqual(ctx.exception.code, "bad_room_key", spec)


class TestParsingHelpers(unittest.TestCase):
    def test_parse_count(self):
        cases = {
            "19": 19,
            "1.2万": 12000,
            "10万+": 100000,
            "1,234": 1234,
            "9.9亿": 990000000,
            "1.2w": 12000,
            "0": 0,
            0: 0,
            7: 7,
            "abc": None,
            "": None,
            None: None,
        }
        for raw, want in cases.items():
            self.assertEqual(bridge.parse_count(raw), want, raw)

    def test_clean_title(self):
        self.assertEqual(bridge.clean_title("  a\n\tb  ", 48), "a b")
        self.assertEqual(bridge.clean_title("abcdef", 3), "abc")
        self.assertIsNone(bridge.clean_title("", 48))
        self.assertIsNone(bridge.clean_title("abc", 0))
        self.assertEqual(bridge.clean_title("a\x00b\x7f", 48), "a b")


class TestBilibiliParse(unittest.TestCase):
    H5 = {
        "code": 0,
        "data": {
            "room_info": {"title": " 测试  房间 ", "live_status": 1, "online": 12345},
            "anchor_info": {"relation_info": {"attention": 9000000}},
            "like_info_v3": {"total_likes": 6789},
        },
    }

    def test_h5(self):
        data = bridge.parse_bilibili_h5(self.H5)
        self.assertEqual(data["online"], 12345)
        self.assertEqual(data["likes"], 6789)
        self.assertEqual(data["fans"], 9000000)
        self.assertEqual(data["status"], 1)
        self.assertEqual(data["title"], "测试 房间")

    def test_h5_offline_keeps_zero(self):
        payload = {
            "code": 0,
            "data": {
                "room_info": {"title": "off", "live_status": 0, "online": 0},
                "like_info_v3": {"total_likes": 0},
            },
        }
        data = bridge.parse_bilibili_h5(payload)
        self.assertEqual(data["online"], 0)
        self.assertEqual(data["likes"], 0)
        self.assertEqual(data["status"], 0)

    def test_h5_code_errors(self):
        for code, want in ((1, "not_found"), (19002000, "not_found"), (-352, "rate_limited")):
            with self.assertRaises(bridge.FetchError) as ctx:
                bridge.parse_bilibili_h5({"code": code})
            self.assertEqual(ctx.exception.code, want, code)

    def test_h5_missing_data_is_parse_error(self):
        with self.assertRaises(bridge.FetchError) as ctx:
            bridge.parse_bilibili_h5({"code": 0, "data": {}})
        self.assertEqual(ctx.exception.code, "parse")

    def test_get_info_fallback_has_no_likes(self):
        payload = {
            "code": 0,
            "data": {
                "room_id": 7734200,
                "title": "T",
                "live_status": 1,
                "online": 100,
                "attention": 200,
            },
        }
        data = bridge.parse_bilibili_room_info(payload)
        self.assertEqual(data["online"], 100)
        self.assertEqual(data["fans"], 200)
        self.assertIsNone(data["likes"])
        self.assertEqual(data["status"], 1)


class TestDouyinParse(unittest.TestCase):
    def test_reflow_live(self):
        payload = {
            "status_code": 0,
            "data": {
                "room": {
                    "status": 2,
                    "title": "dy room",
                    "user_count": 19,
                    "like_count": 17292,
                    "stats": {"user_count_str": "19"},
                    "owner": {"follow_info": {"follower_count": 5123142}},
                }
            },
        }
        data = bridge.parse_douyin_reflow(payload)
        self.assertEqual(data["online"], 19)
        self.assertEqual(data["likes"], 17292)
        self.assertEqual(data["fans"], 5123142)
        self.assertEqual(data["status"], 1)

    def test_reflow_ended_keeps_zero_online(self):
        payload = {
            "status_code": 0,
            "data": {
                "room": {
                    "status": 4,
                    "title": "ended",
                    "user_count": 0,
                    "like_count": 1107004,
                    "stats": {"user_count_str": "0"},
                }
            },
        }
        data = bridge.parse_douyin_reflow(payload)
        self.assertEqual(data["online"], 0)
        self.assertEqual(data["likes"], 1107004)
        self.assertEqual(data["status"], 0)

    def test_reflow_invalid_room(self):
        with self.assertRaises(bridge.FetchError) as ctx:
            bridge.parse_douyin_reflow({"status_code": 10011})
        self.assertEqual(ctx.exception.code, "invalid_room")

    def test_enter_parses_wan_string(self):
        payload = {
            "status_code": 0,
            "data": {
                "data": [
                    {
                        "status": 2,
                        "title": "enter room",
                        "user_count_str": "1.2万",
                        "like_count": 5,
                    }
                ]
            },
        }
        data = bridge.parse_douyin_enter(payload)
        self.assertEqual(data["online"], 12000)
        self.assertEqual(data["likes"], 5)
        self.assertEqual(data["status"], 1)

    def test_enter_empty_room_list(self):
        with self.assertRaises(bridge.FetchError) as ctx:
            bridge.parse_douyin_enter({"status_code": 0, "data": {"data": []}})
        self.assertEqual(ctx.exception.code, "not_found")

    def test_zero_byte_is_rejection(self):
        self.assertTrue(bridge.douyin_rejected(200, b""))
        self.assertTrue(bridge.douyin_rejected(200, b"  "))
        self.assertTrue(bridge.douyin_rejected(403, b"<html>"))
        self.assertTrue(bridge.douyin_rejected(429, b""))
        self.assertFalse(bridge.douyin_rejected(200, b'{"status_code":0}'))


class TestKuaishouParse(unittest.TestCase):
    def test_live_room(self):
        data = bridge.parse_kuaishou_state(
            {
                "found": True,
                "hasLiveroom": True,
                "isLiving": True,
                "title": "ks live",
                "watching": "1,376",
                "likes": "7,322",
            }
        )
        self.assertEqual(data["online"], 1376)
        self.assertEqual(data["likes"], 7322)
        self.assertEqual(data["status"], 1)
        self.assertIsNone(data["fans"])

    def test_live_room_wan_string(self):
        data = bridge.parse_kuaishou_state(
            {"found": True, "hasLiveroom": True, "isLiving": True, "watching": "1.2万"}
        )
        self.assertEqual(data["online"], 12000)

    def test_offline_room_reports_status_zero_without_numbers(self):
        data = bridge.parse_kuaishou_state(
            {"found": True, "hasLiveroom": True, "isLiving": False, "title": "off"}
        )
        self.assertEqual(data["status"], 0)
        self.assertIsNone(data["online"])

    def test_rate_limited_page(self):
        with self.assertRaises(bridge.FetchError) as ctx:
            bridge.parse_kuaishou_state(
                {"found": True, "hasLiveroom": True, "errorType": 2}
            )
        self.assertEqual(ctx.exception.code, "rate_limited")

    def test_missing_state(self):
        for raw in ({}, {"found": False}, None, "x"):
            with self.assertRaises(bridge.FetchError) as ctx:
                bridge.parse_kuaishou_state(raw)
            self.assertEqual(ctx.exception.code, "kuaishou_no_data", raw)


class TestBridgeLogic(unittest.TestCase):
    def setUp(self):
        self.cfg = bridge.Config()
        self.bridge = bridge.Bridge(self.cfg)

    def tearDown(self):
        self.bridge.close()

    def test_unsupported_platform(self):
        with self.assertRaises(bridge.FetchError) as ctx:
            self.bridge._fetch("weibo:1")
        self.assertEqual(ctx.exception.code, "unsupported_platform")

    def test_invalid_rooms_never_hit_network(self):
        for key in ("bilibili:abc", "bilibili:1234567890123", "douyin:12ab", "kuaishou:a"):
            with self.assertRaises(bridge.FetchError) as ctx:
                self.bridge._fetch(key)
            self.assertEqual(ctx.exception.code, "invalid_room", key)

    def test_kuaishou_without_playwright(self):
        if bridge.Bridge(self.cfg).has_playwright:
            self.skipTest("playwright is installed")
        with self.assertRaises(bridge.FetchError) as ctx:
            self.bridge._fetch("kuaishou:3xwnf96yetb5ewm")
        self.assertEqual(ctx.exception.code, "kuaishou_requires_browser")

    def test_effective_ttl_respects_platform_minimum(self):
        self.assertEqual(self.bridge._effective_ttl("bilibili:6"), 20.0)
        self.assertEqual(self.bridge._effective_ttl("douyin:7076284313080843019"), 60.0)
        slow = bridge.Bridge(bridge.Config(cache_ttl=30.0))
        try:
            self.assertEqual(slow._effective_ttl("bilibili:6"), 30.0)
            self.assertEqual(slow._effective_ttl("kuaishou:abc"), 60.0)
        finally:
            slow.close()

    def test_bilibili_h5_fallback_to_get_info(self):
        calls = []

        def fake_get(url, headers, timeout, max_bytes, retries=0):
            calls.append(url)
            if "getH5InfoByRoom" in url:
                return 200, {}, b'{"code":-352,"msg":"-352"}'
            return 200, {}, json.dumps(
                {
                    "code": 0,
                    "data": {
                        "room_id": 7734200,
                        "title": "fallback",
                        "live_status": 1,
                        "online": 100,
                        "attention": 200,
                    },
                }
            ).encode()

        with mock.patch.object(bridge, "http_get", fake_get):
            data = self.bridge._fetch_bilibili("6")
        self.assertEqual(len(calls), 2)
        self.assertEqual(data["online"], 100)
        self.assertIsNone(data["likes"])
        self.assertEqual(self.bridge._bili_h5_fails, 1)

    def test_bilibili_h5_success_no_fallback(self):
        calls = []

        def fake_get(url, headers, timeout, max_bytes, retries=0):
            calls.append(url)
            return 200, {}, json.dumps(TestBilibiliParse.H5).encode()

        with mock.patch.object(bridge, "http_get", fake_get):
            data = self.bridge._fetch_bilibili("6")
        self.assertEqual(len(calls), 1)
        self.assertEqual(data["online"], 12345)

    def test_douyin_enter_uses_anonymous_ttwid(self):
        seen = {}

        def fake_head(url, headers, timeout):
            self.assertEqual(url, bridge.DY_TTWID_URL)
            return 404, {}, ["ttwid=1%7Canon%3D; Domain=.douyin.com; Path=/; HttpOnly"]

        def fake_get(url, headers, timeout, max_bytes, retries=0):
            seen["cookie"] = headers.get("Cookie")
            seen["referer"] = headers.get("Referer")
            return 200, {}, json.dumps(
                {
                    "status_code": 0,
                    "data": {
                        "data": [
                            {
                                "status": 2,
                                "title": "t",
                                "user_count_str": "77",
                                "like_count": 3,
                            }
                        ]
                    },
                }
            ).encode()

        with mock.patch.object(bridge, "http_head", fake_head):
            with mock.patch.object(bridge, "http_get", fake_get):
                data = self.bridge._fetch_douyin("611063790284")
        self.assertEqual(seen["cookie"], "ttwid=1%7Canon%3D")
        self.assertEqual(seen["referer"], "https://live.douyin.com/611063790284")
        self.assertEqual(data["online"], 77)

    def test_douyin_zero_byte_blocks_without_retry(self):
        calls = []

        def fake_head(url, headers, timeout):
            return 404, {}, ["ttwid=x"]

        def fake_get(url, headers, timeout, max_bytes, retries=0):
            calls.append(url)
            return 200, b"", b""

        with mock.patch.object(bridge, "http_head", fake_head):
            with mock.patch.object(bridge, "http_get", fake_get):
                with self.assertRaises(bridge.FetchError) as ctx:
                    self.bridge._fetch_douyin("611063790284")
                self.assertEqual(ctx.exception.code, "rate_limited")
                # Second attempt must short-circuit on the global block.
                with self.assertRaises(bridge.FetchError) as ctx2:
                    self.bridge._fetch_douyin("611063790285")
                self.assertEqual(ctx2.exception.code, "rate_limited")
        self.assertEqual(len(calls), 1)

    def test_deadline_returns_timeout_then_background_fills_cache(self):
        b = bridge.Bridge(bridge.Config(deadline=0.2))

        def slow_fetch(key):
            time.sleep(0.6)
            return {"online": 5}

        try:
            with mock.patch.object(b, "_fetch", slow_fetch):
                started = time.monotonic()
                first = b.collect(["bilibili:6"], deadline=0.2)
                self.assertLess(time.monotonic() - started, 0.5)
                self.assertFalse(first["bilibili:6"].ok)
                self.assertEqual(first["bilibili:6"].error, "timeout")
                time.sleep(0.6)  # let the background fetch land in the cache
                second = b.collect(["bilibili:6"], deadline=0.2)
                self.assertTrue(second["bilibili:6"].ok)
                self.assertEqual(second["bilibili:6"].data["online"], 5)
        finally:
            b.close()

    def test_stale_value_served_while_refreshing(self):
        b = bridge.Bridge(bridge.Config(deadline=0.1, cache_ttl=0.05, stale_max=120.0))
        try:
            now = time.monotonic()
            b._cache_put(
                "weibo:1",
                bridge.Entry(
                    ok=True,
                    data={"online": 42},
                    error=None,
                    attempt_at=now - 5.0,
                    ok_at=now - 5.0,
                ),
            )
            with mock.patch.object(b, "_fetch", lambda key: time.sleep(0.8) or {"online": 1}):
                entries = b.collect(["weibo:1"], deadline=0.1)
            self.assertTrue(entries["weibo:1"].ok)
            self.assertEqual(entries["weibo:1"].data["online"], 42)
        finally:
            b.close()


class TestRender(unittest.TestCase):
    def setUp(self):
        self.bridge = bridge.Bridge(bridge.Config())
        self.keys = ["bilibili:%d" % i for i in range(1, 9)]

    def tearDown(self):
        self.bridge.close()

    def test_contract_fields_and_zero(self):
        entries = {
            "bilibili:1": _entry(
                {"online": 0, "likes": 0, "fans": 0, "status": 0, "title": "t"}
            )
        }
        payload, body = self.bridge.render(["bilibili:1"], entries)
        room = payload["rooms"][0]
        self.assertEqual(
            list(room.keys()),
            ["key", "platform", "room", "ok", "online", "likes", "fans", "status", "title"],
        )
        self.assertEqual(room["online"], 0)
        self.assertEqual(room["likes"], 0)
        self.assertEqual(room["status"], 0)
        self.assertTrue(payload["ok"])
        self.assertIsInstance(payload["ts"], int)

    def test_missing_metrics_are_omitted(self):
        payload, _ = self.bridge.render(["bilibili:1"], {"bilibili:1": _entry({"online": 5})})
        self.assertEqual(payload["rooms"][0], {"key": "bilibili:1", "platform": "bilibili", "room": "1", "ok": True, "online": 5})

    def test_error_room(self):
        payload, _ = self.bridge.render(
            ["douyin:611063790284"], {"douyin:611063790284": _entry(error="rate_limited")}
        )
        self.assertEqual(
            payload["rooms"][0],
            {
                "key": "douyin:611063790284",
                "platform": "douyin",
                "room": "611063790284",
                "ok": False,
                "error": "rate_limited",
            },
        )

    def test_payload_capped_at_4kb(self):
        big = bridge.Config(title_max=200)
        b = bridge.Bridge(big)
        try:
            entries = {
                key: _entry({"online": 1, "likes": 2, "fans": 3, "status": 1, "title": "长标题" * 80})
                for key in self.keys
            }
            payload, body = b.render(self.keys, entries)
            self.assertLessEqual(len(body), bridge.MAX_RESPONSE_BYTES)
            self.assertEqual(len(payload["rooms"]), 8)
            for room in payload["rooms"]:
                self.assertLessEqual(len(room.get("title", "")), 24)
        finally:
            b.close()

    def test_no_titles_fits(self):
        b = bridge.Bridge(bridge.Config(title_max=0))
        try:
            entries = {key: _entry({"online": 1, "title": "x" * 100}) for key in self.keys}
            payload, body = b.render(self.keys, entries)
            self.assertLessEqual(len(body), bridge.MAX_RESPONSE_BYTES)
            self.assertNotIn("title", payload["rooms"][0])
        finally:
            b.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
