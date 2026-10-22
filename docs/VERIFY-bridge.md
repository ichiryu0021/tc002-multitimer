# Bridge 服务实测与部署验证（task-3 / bridge-verify）

> 状态：**完成**（2026-10-22 19:32–19:40，Asia/Shanghai）。
> 验证对象：`tools/live-bridge`（bridge.py 3.0.0 + test_bridge.py），协议基准
> [`V3-CONTRACT.md`](V3-CONTRACT.md) §5、部署文档 [`LIVE-BRIDGE.md`](LIVE-BRIDGE.md)。
> 边界：**只读源码**（未改 `bridge.py` 等任何源码）；**只新增本文件**；未使用账号/cookie；
> 未刷机、未重启设备、未映射公网。测试用 bridge 只绑 `127.0.0.1:8081`（刻意不开局域网）。

## 0. 结论速览

| 项 | 结果 |
|---|---|
| 离线单测 | **40/40 通过**（`Ran 40 tests in 0.930s`，7 个测试类，全部 mock 无网络/无浏览器） |
| 契约响应 | `/health`、`/live`（B 站单房间 / B 站+抖音混排 / 8 房间 / 未开播 / 不存在 / 快手 / 未知平台）全部符合契约，**所有响应 ≤ 4096 B**（实测最大 1261 B） |
| 0 值语义 | `online/likes = 0` 且 `ok:true`（未开播）与 `ok:false`（不存在/失败）区分清楚，与文档一致 |
| 错误路径 | `missing_rooms` / `bad_room_key` / `too_many_rooms` → 400；404、405、414 均 JSON；**HEAD/PUT/DELETE → 501 HTML**（与文档“405 非 GET”不符，见 §8 D1） |
| 设备报文 | HTTP/1.0 + `Accept` + `Connection: close` 原始 socket 读到 EOF 完整可用：`Content-Length` 与实际一致、无 chunked、JSON 可解析 |
| 限流/缓存/超时 | 代码常量与文档 §1.3 表一致（TTL=20 s 且不低于平台最小间隔、deadline 2.5 s、上游 4 s、熔断 180 s、缓存 64、single-flight） |
| 设备侧 B 站边界 | **真机只读确认**：`/lib/libssl.so.1.1` 235,776 B、`/res/ui/cacert.pem` 226,168 B（145 CA）存在且与工作区备份 MD5 一致；用该 CA 文件实测 `api.live.bilibili.com`/`live.bilibili.com` **TLS1.2 校验通过** |
| 快手 | 本机无 Playwright：`kuaishou_requires_browser`，**0 网络请求、~2 ms**；浏览器抓取路径未实测（文档已注明） |
| 文档一致性 | 启动/CLI/响应/限流/错误码基本一致；发现 **8 处需修正/补充**（均为 P3，无 P0/P1，见 §8.2） |

**判定：v3.0 设备用 `live.source=hybrid` 接 bridge 的数据面已可用**；剩余风险集中在第三方平台风控/接口变更与设备端真机联调（B 站直连 TLS 只验证了前提，未在设备内跑握手）。

## 1. 验证环境与边界

- 主机：Windows 11 Pro `10.0.26300`，Python **3.14.7**，requests **2.34.2**，curl **8.21.0**；
  **无 Docker**、**无 Playwright**；主机 IP `192.168.31.66`（以太网 4），网络配置文件 = Private。
- 设备：TC002（`Zkswe_SSD21X_SPINOR`，固件 flythingsV2.1）`192.168.31.124:5555`，
  仅用 `tools/platform-tools/adb.exe` 做**只读**检查（`ls -l`、`adb pull`、`date`、`cat`），结束后已 `adb disconnect`。
- 被测源码：`bridge.py` SHA-256 `1D476794C60D0B87AEB346E9B8BB360647CAED4D1B77EA1F1B89D5E4F1C0DCBF`
  （`tools/live-bridge/` 整目录当前为未跟踪文件，本次未做任何修改）。
- 测试用服务：`python bridge.py --host 127.0.0.1 --port 8081 --log-level INFO`（前台后台 job，
  验证结束已停）。**部署时必须用默认 `0.0.0.0`，否则设备连不上**（见 §11）。
- 未做：真机刷机/部署、设备端 B 站直连代码联调、Playwright 抓取、Docker、公网访问、账号/cookie 相关操作。

## 2. 离线单测（40 tests）

```powershell
cd D:\Project\ulanzi\tools\live-bridge
python -m unittest -v test_bridge
```

输出摘要：

```text
Ran 40 tests in 0.930s

OK
```

- 测试类 7 个：`TestParseRooms`、`TestParsingHelpers`、`TestBilibiliParse`、`TestDouyinParse`、
  `TestKuaishouParse`、`TestBridgeLogic`、`TestRender`。
- 离线隔离已核实：测试用 `unittest.mock.patch.object` 替换 `http_get`/`http_head`/`_fetch`
  （如 `test_bridge.py:332/377/413/440`），不联网、不启动浏览器。
- 覆盖点与文档 §9.1 描述一致：房间参数解析/上限/非法字符、B 站 h5+兜底解析、`code` 映射、
  抖音两条路径解析与 0 字节判定、限流熔断只发一次请求、快手状态映射、4 KB 裁剪、0 值保留、
  缺字段省略、deadline 超时 + 后台回填 + 旧值兜底。
- 结果：**40/40 通过**，与文档 §9.1 的 `Ran 40 tests in 0.924s / OK` 一致（本次 0.930 s）。

## 3. 启动、健康检查与进程指标

启动命令与日志（与文档 §2.1 的启动日志逐行吻合，仅 host 不同）：

```text
19:32:27 INFO    MultiTimer live bridge 3.0.0 listening on 127.0.0.1:8081
19:32:27 INFO    cache ttl 20s (per-platform minimum: {'bilibili': 10.0, 'douyin': 60.0, 'kuaishou': 60.0})
19:32:27 INFO    response deadline 2.5s, upstream timeout 4.0s
19:32:27 INFO    device url: http://192.168.31.66:8081/live?rooms=bilibili:6
19:32:27 INFO    kuaishou: Playwright not installed -> kuaishou rooms return kuaishou_requires_browser
```

`GET /health`（200，**172 B**，本机 ~2 ms）：

```json
{"ok":true,"ts":1792668766,"version":"3.0.0","uptimeSec":19,"maxRooms":8,"cachedRooms":0,"platforms":{"bilibili":"ready","douyin":"ready","kuaishou":"requires_playwright"}}
```

运行期指标（验证末段）：RSS **47.3 MB**、线程 12→7（空闲）、`uptimeSec=247`、`cachedRooms=17`。
字段形状与文档 §9.5 的 `/health` 一致；`kuaishou=requires_playwright` 与本机未装 Playwright 一致。
内存与文档 §1.3“常驻内存约十几 MB”有偏差（见 §8.2 D3）。

## 4. 契约响应实测

所有请求：`curl.exe -sS -D headers -o body -w '%{http_code}|%{time_total}|%{size_download}' <url>`，本机回环。

### 4.1 正常数据

| # | 请求 | HTTP | Content-Length / body | 耗时 | 关键字段（ts 快照） |
|---|---|---|---|---|---|
| 1 | `/live?rooms=bilibili:6`（冷） | 200 | **216 B** | **122 ms** | `ok:true`；`online=204644, likes=4044425, fans=16896342, status=1`，有 title |
| 2 | 同上（20 s 内二次） | 200 | 216 B | **2 ms** | 命中缓存（有效 TTL = max(20 s, B 站 10 s) = 20 s），数值不变 |
| 3 | `/live?rooms=bilibili:6,douyin:7076284313080843019` | 200 | **407 B** | **313 ms** | B 站同 #1；抖音 19 位 `ok:true, online=0, likes=1107004, fans=5123119, status=0`（未开播，0 为有效值） |
| 4 | `/live?rooms=bilibili:3,bilibili:21452505` | 200 | **324 B** | 127 ms | 两个未开播房间均 `ok:true, online=0, likes=0, status=0`（fans 正常） |
| 5 | `/live?rooms=bilibili:999999999999` | 200 | 144 B | 123 ms | `ok:false, error:"not_found"`（h5 失败→`get_info` 兜底仍 not_found） |
| 6 | `/live?rooms=kuaishou:3xwnf96yetb5ewm` | 200 | **166 B** | 2 ms | `ok:false, error:"kuaishou_requires_browser"`，日志 `in 0ms` → 未发网络请求 |
| 7 | `/live?rooms=youtube:abc` | 200 | 135 B | 2 ms | `ok:false, error:"unsupported_platform"` |
| 8 | `/live?rooms=bilibili:6,bilibili:3,bilibili:21452505,bilibili:7734200,bilibili:5440,bilibili:1,bilibili:2,bilibili:4` | 200 | **1261 B** | 183 ms | 8 房间（协议上限），含 `not_found` 房间，整体仍 200；远小于 4096 B 上限 |
| 9 | `/live?rooms=douyin:611063790284`（12 位 web_rid，单次） | 200 | 192 B | **897 ms** | `ok:true, online=100, likes=25333, status=1`，**无 `fans` 字段**（与文档 §3.2 一致；匿名 ttwid 只在内存） |
| 10 | `/live?rooms=kuaishou:...,douyin:abc`（两房间全失败） | 200 | 254 B | 1.8 ms | 顶层 `ok:true`，两个 `rooms[].ok:false`（正常请求一律 200） |
| 11 | `/live?rooms=bilibili:6,bilibili:6`（重复） | 200 | 216 B | 85 ms | 去重为 1 个房间 |
| 12 | `/live?rooms=BILIBILI:6`（大写平台） | 200 | 216 B | 1.6 ms | 规范化成 `bilibili:6` 后命中缓存 |
| 13 | `/live?rooms=douyin:7076284313080843019,bilibili:6`（顺序） | 200 | 407 B | 1.5 ms | 响应顺序 = 请求顺序（抖音在前） |

响应体示例（#1，公开数据，无 cookie/凭据）：

```json
{"ok":true,"ts":1792668766,"rooms":[{"key":"bilibili:6","platform":"bilibili","room":"6","ok":true,"online":204644,"likes":4044425,"fans":16896342,"status":1,"title":"【预告】24号1点全球总决赛瑞士轮"}]}
```

### 4.2 0 值 / 未开播 / 不存在 / 失败占位（契约核心语义）

| 场景 | 实测响应 | 设备应显示 |
|---|---|---|
| 未开播（B 站 `bilibili:3`） | `ok:true, online:0, likes:0, fans:213675, status:0` | 数值 **0**（有效值，不是 `--`） |
| 已结束（抖音 19 位） | `ok:true, online:0, likes:1107004, fans:5123119, status:0` | 数值 0 / 历史点赞；不是失败 |
| 房间不存在（`bilibili:999999999999`） | `ok:false, error:"not_found"` | `--` |
| 快手无浏览器 | `ok:false, error:"kuaishou_requires_browser"` | `--` |
| 未知平台 | `ok:false, error:"unsupported_platform"` | `--` |
| 平台不提供的指标 | 字段**缺失**（如 12 位抖音无 `fans`） | `--`（不能用 0 顶替） |

结论：与 `V3-CONTRACT.md` §5“数值 0 是有效值、不能用 0 判断失败”一致。

### 4.3 报文体大小

所有实测 body（含 `/health`、错误 JSON、8 房间混合）**均 ≤ 4096 B**，最大为 8 房间的 **1261 B**；
2 房间 ~407 B，与“设备一页 2 个房间”的用量余量充足。4 KB 裁剪逻辑（`render()`：`title-max`→24 字符→去掉 title）
由单测 `test_payload_capped_at_4kb` 覆盖，本次线上未触发。

## 5. 错误路径与 HTTP 语义

| 请求 | HTTP | Content-Length | body | 与文档 |
|---|---|---|---|---|
| `GET /live`（无 `rooms`） | **400** | 36 B | `{"ok":false,"error":"missing_rooms"}` | 一致 |
| `GET /live?rooms=`（空值） | 400 | — | `{"ok":false,"error":"missing_rooms"}` | 一致 |
| `GET /live?rooms=bilibili:6;rm` | **400** | 35 B | `{"ok":false,"error":"bad_room_key"}` | 一致 |
| `GET /live?rooms=<9 个房间>` | **400** | 37 B | `{"ok":false,"error":"too_many_rooms"}` | 一致 |
| `GET /nope` | **404** | 32 B | `{"ok":false,"error":"not_found"}` | 一致 |
| `POST /live` | **405** | 41 B | `{"ok":false,"error":"method_not_allowed"}` | 一致 |
| `HEAD /live`、`PUT /live`、`DELETE /health` | **501** | 481–484 B | **HTML** `Error response / Unsupported method` | **不一致**（文档称“405 非 GET”），见 §8 D1 |
| `GET /live?rooms=<query >1024 B>` | **414** | 35 B | `{"ok":false,"error":"uri_too_long"}` | 文档未收录 414/该错误码，见 §8 D2 |
| `GET /live?rooms=bilibili:abc`（房间号非法） | **200** | — | `{"ok":true,...,"ok":false,"error":"invalid_room"}` | 一致（房间级错误，HTTP 仍 200） |
| `GET /live?rooms=douyin:abc` / `kuaishou:a` / `bilibili:12345678901234` | 200 | — | 各房间 `invalid_room` | 一致 |
| `GET /live?rooms=...`（1200 字符 path） | **404** | 32 B | `not_found` | 说明 1 KB 上限只作用于 query，见 §8 D2 |

所有 400/404/405/414 JSON 响应头：`Content-Type: application/json; charset=utf-8`、
`Content-Length`、`Cache-Control: no-store`、`Connection: close`；501 为 `text/html;charset=utf-8`。

## 6. 设备报文形状（HTTP/1.0 + Accept + Connection: close）

设备契约报文（`V3-CONTRACT.md` §5）模拟：

```powershell
curl.exe --http1.0 -H "Accept: application/json" -H "Connection: close" -D - "http://127.0.0.1:8081/live?rooms=bilibili:6"
```

响应头（实测原文）：

```http
HTTP/1.1 200 OK
Server: MultiTimerLiveBridge/3.0.0
Date: Thu, 22 Oct 2026 11:33:11 GMT
Content-Type: application/json; charset=utf-8
Content-Length: 216
Cache-Control: no-store
Connection: close
```

原始 TCP socket（发送 `GET /live?rooms=bilibili:6 HTTP/1.0` + `Host` + `Accept: application/json` +
`Connection: close`，随后**读到 EOF**）实测：

- 状态行：`HTTP/1.1 200 OK`；`Content-Length: 216`；**无 `Transfer-Encoding`**；连接被服务端关闭；
- 按 EOF 收到的 body 实际 216 B，`json_ok=True`，声明长度 = 实际长度；耗时 0.014 s。

结论：**HTTP/1.0 客户端按 Content-Length/EOF 读取可完整拿到响应**，无 chunked。
注意：服务端响应行版本是 **HTTP/1.1**（`BaseHTTPRequestHandler.protocol_version="HTTP/1.1"`），
不是 HTTP/1.0；文档未承诺响应版本。设备侧 v3 `HttpClient`（当前未提交代码，只读抽查
`native/MultiTimer/src/net/HttpClient.cpp:401-499`）解析状态行只要求前缀 `HTTP/`、并按
`Content-Length` 截取 body，因此**兼容**；上线前仍建议在真机跑一次（留给 QA/device 任务）。

## 7. 缓存 / 限流 / 超时 / 4 KB —— 文档 §1.3 与代码核对

| 文档 §1.3 / §4 项 | 代码实现（bridge.py） | 实测/核对 |
|---|---|---|
| 缓存 TTL 默认 20 s；有效值 = max(--cache-ttl, 平台最小间隔) | `DEFAULT_CACHE_TTL=20`；`MIN_INTERVAL={bilibili:10, douyin:60, kuaishou:60}`；`_effective_ttl()` | 二次请求 2 ms 命中；启动日志打印各平台最小值 |
| 上游超时 4 s，connect ≤3 s | `DEFAULT_TIMEOUT=4.0`；`http_get(timeout=(min(3,timeout), timeout))` | 代码核对 ✓ |
| 响应预算 2.5 s | `DEFAULT_DEADLINE=2.5`，clamp 0.5–15 s；`collect(deadline)` | 实测最大耗时 897 ms（12 位抖音），均 < 2.5 s |
| 超时兜底 ≤120 s 旧值 | `DEFAULT_STALE_MAX=120.0`；`collect()` 在途时返回 `ok_at` 未超 stale 的旧值 | 代码核对 ✓ |
| 单房间 single-flight | `_inflight` + `_submit()` | 代码核对 ✓ |
| 失败不立即重试 | 所有 `http_get`/`http_head` 调用 `retries=0`；B 站端点级降级重试 | 代码核对 ✓ |
| 抖音 200+0B/403/429 → 全平台暂停 180 s | `DEFAULT_RATE_BLOCK=180`；`douyin_rejected()` + `_douyin_block_until` | 代码核对 ✓（未主动触发线上限流） |
| B 站 h5 连续 2 次非房间级失败 → 600 s 只用 `get_info` | `_bilibili_note_h5_failure()` + `_bili_down_until=+600` | 代码核对 ✓ |
| 缓存条目 ≤64（LRU 式） | `MAX_CACHE_ENTRIES=64`；`_cache_put()` 淘汰最旧 | 代码核对 ✓ |
| 最多 8 房间 | `MAX_ROOMS=8`；第 9 个 → `too_many_rooms` | 实测 400 ✓ |
| 请求 query 上限 1024 B | `MAX_QUERY_BYTES=1024` → 414 `uri_too_long` | 实测 414（文档表述见 D2） |
| 响应体硬上限 4096 B | `MAX_RESPONSE_BYTES=4096`；`render()` 三段裁剪 | 实测全部 ≤1261 B；单测覆盖裁剪 |
| 线程模型：ThreadingHTTPServer + 8 workers | `daemon_threads`、`request_queue_size=16`、`DEFAULT_WORKERS=8` | 实测线程数 12→7 |
| 单响应体 256 KB–2 MB 上限流式读取 | B 站 256 KB、抖音 reflow 512 KB、抖音 enter 2 MB | 代码核对 ✓ |
| 访问日志不含 query、不含 cookie | `LOG.info("%s %s -> %d %dms client=%s", method, path.split("?")[0], ...)`；ttwid 日志只打字节数 | 实测日志无 query、无 token（仅 `ttwid refreshed (memory only, 127 bytes)`） |

## 8. 文档一致性核对（启动 / 防火墙 / systemd / Docker / 限流 / CLI / 错误码）

### 8.1 一致项（已逐条核对）

- **启动**：默认 `0.0.0.0:8081`、`--host/--port` 生效、启动日志与 §2.1 逐行一致（host 按测试改为 127.0.0.1）；
  `--version` → `bridge.py 3.0.0`；requirements 已满足（requests 2.34.2）。
- **CLI（§4）**：`--cache-ttl 20`、`--timeout 4.0`、`--deadline-ms 2500`、`--stale-max 120`、
  `--rate-block 180`、`--workers 8`、`--title-max 48`、`--log-level INFO`、`--once`、`--rooms bilibili:6`、
  `--pretty`、`--host 0.0.0.0`、`--port 8081` 全部与 argparse 实际一致（`--help` 核对）。
- **`--once` 退出码**：B 站成功 → `0`（payload=216 B）；快手无浏览器 → `2`（payload=166 B）；
  非法房间 key → `2`（`{"ok":false,"error":"bad_room_key"}`）；绑定失败（`--host 10.255.255.1`）→ **`1`**，
  日志 `cannot bind ... [WinError 10049]`，与 §4 一致。
- **错误码（§8）实测子集**：`not_found`、`invalid_room`、`unsupported_platform`、`kuaishou_requires_browser`、
  `missing_rooms`、`bad_room_key`、`too_many_rooms`、`method_not_allowed`、`uri_too_long` 均按文档语义返回。
- **B 站（§3.1）**：h5 主端点 + `get_info` 兜底、`code` 映射（1/19002000/19002001/-400→not_found，
  -352/-412→rate_limited）、房间号 1–12 位纯数字，代码核对 + 线上 not_found/未开播实测一致。
- **抖音（§3.2）**：两条路径实测均通过——19 位 `room_id` 走 `reflow`（无需 cookie，0 值有效）；
  12 位 `web_rid` 走网页 `enter`（匿名 ttwid 仅内存、2 h 过期，响应无 `fans`）；0 字节判定与 180 s 熔断代码核对一致。
- **快手（§3.3）**：未装 Playwright → `kuaishou_requires_browser`，**不发网络请求**（0 ms）；与本机状态一致。
- **安全（§6）**：`POST /live` → 405、服务只读；本机日志无 query/cookie；ttwid 不落盘（代码 `self._douyin_ttwid` 仅内存 + 日志只打长度）。
- **防火墙（§5.1/§5.2）**：命令语法核对无误（`New-NetFirewallRule` / `netsh advfirewall`）；本机当前
  **没有**该规则、网络配置文件为 **Private**（`Get-NetConnectionProfile`），说明部署时需按文档加规则。
- **systemd（§7.2）**：单元文件字段与代码 CLI 匹配（`WorkingDirectory` + `bridge.py --host 0.0.0.0 --port 8081`、
  `Restart=on-failure`）；未在 Linux 实跑。
- **Docker（§7.3）**：两个 Dockerfile 的依赖/CMD 与代码一致（`requests` 必需、Playwright 懒启动、
  `--host 0.0.0.0`）；本机无 Docker，与文档“可选，未在本机验证”一致。
- **自测记录（§9）**：§9.1 40 tests ✓；§9.2 `Content-Length: 216` ✓（本次同为 216 B，冷启动 122 ms）；
  §9.3 抖音两条路径 ✓；§9.4 快手 payload=166 B ✓；§9.5 错误形状 ✓、HTTP/1.0 可读 ✓。

### 8.2 不一致 / 需补充项（均为 P3 文档级，无 P0/P1）

| ID | 位置 | 文档写法 | 实际行为（实测/代码） | 建议 |
|---|---|---|---|---|
| D1 | LIVE-BRIDGE §1.2.5 | “`405` 非 GET” | 仅 **POST → 405 JSON**；`HEAD/PUT/DELETE` 落到 `BaseHTTPRequestHandler` 默认 → **501 HTML**（481–484 B） | 改为“仅 POST 返回 405；其它方法 501（设备不会使用）” |
| D2 | LIVE-BRIDGE §1.1 / §1.2.5 / §8 | “请求行总长上限 1 KB”；错误码表无 414 | 实际只限制 **query 长度 >1024 B → 414 `uri_too_long`**；1200 字符 path 返回 404；§8 未收录该错误码/状态码 | 改为“query 上限 1024 B”，并在 §8 增补 `uri_too_long`（414） |
| D3 | LIVE-BRIDGE §1.3 | “常驻内存约十几 MB” | 实测 RSS **47.3–47.4 MB**（Python 3.14 + requests；线程 12→7） | 改成“约 45–50 MB（Python 基线）”，NAS/小机器按此估算 |
| D4 | LIVE-BRIDGE §7.2 / §7.3 | systemd 直接 `User=multitimer`；Playwright Docker 段只有 Dockerfile | 未说明需先创建 `multitimer` 用户；Playwright 镜像段缺 `docker build -t live-bridge-ks .` 步骤，且 Docker 未验证 | 补 `useradd` 一行与 build 命令；标注 Docker 未验证 |
| D5 | 部署行为（§2.4 相关） | “若被占用，换 `--port 8082`” | Windows 上 `allow_reuse_address=True` 使**第二个实例也能 bind 同一 127.0.0.1:8081**（实测两个 PID 同时 LISTENING），不会以“端口占用”退出 | 部署脚本启动前用 `netstat -ano | findstr :8081` / `Get-NetTCPConnection` 检查；不要依赖启动失败发现重复实例 |
| D6 | LIVE-BRIDGE §9 | 自测环境“Windows 10” | 本机实测 **Windows 11 Pro 10.0.26300** | 若为同一台机器，更新环境描述 |
| D7 | 未文档化 | 未提根路径 | `GET /` → 200 `{"ok":true,"service":"multitimer-live-bridge",...}` | 可选：在 §1 提一句（无凭据、无害） |
| D8 | V3-CONTRACT §5 示例 | kuaishou 失败示例 `"error":"signature"` | 实现实际为 `kuaishou_requires_browser` 等（以 LIVE-BRIDGE §8 为准） | 把示例改成 §8 的真实错误码，避免设备端按 “signature” 判断 |
| — | LIVE-BRIDGE §9.5 | 2 房间 370 B / 8 房间 1344 B | 本次 2 房间 407 B / 8 房间 1261 B（房间与标题不同） | **非矛盾**，仅为时点/房间差异；正文已注明 |
| — | §1.2.5 响应版本 | 只承诺 `Content-Length`/`Connection: close`/无 chunked | 响应行是 `HTTP/1.1`（设备发 1.0 亦如此） | 文档不矛盾；设备端兼容性见 §6 |

## 9. 设备侧 B 站直连边界（只读检查，无账号/cookie）

命令（只读；结束后 `adb disconnect`）：

```powershell
$adb = "D:\Project\ulanzi\tools\platform-tools\adb.exe"
& $adb connect 192.168.31.124:5555
& $adb -s 192.168.31.124:5555 shell "ls -l /lib/libssl* /lib/libcrypto* /res/ui/cacert.pem; date"
& $adb -s 192.168.31.124:5555 pull /lib/libssl.so.1.1  $env:TEMP\bridge-verify\device\
& $adb -s 192.168.31.124:5555 pull /res/ui/cacert.pem $env:TEMP\bridge-verify\device\
& $adb disconnect 192.168.31.124:5555
```

实测结果（真机）：

| 项 | 结果 |
|---|---|
| `/lib/libssl.so` | 符号链接 → `libssl.so.1.1`，**235,776 B**，MD5 `97BD81A70213B539491CC6E95035E28C`（与工作区 `build/verify/libssl.so.1.1` 一致） |
| `/lib/libcrypto.so.1.1` | **875,672 B** 存在 |
| `/res/ui/cacert.pem` | **226,168 B**，MD5 `3DB3415EA8A03AF2895E4D16A605205A`（与 `build/flash/res-stock/ui/cacert.pem` 一致）；含 **145 张** CA 证书 |
| OpenSSL 版本 | 目标库字符串 `OpenSSL 1.1.0i  14 Aug 2018`（TLS 1.2，无 TLS 1.3） |
| 设备时间 | `Thu Oct 22 11:33:48 UTC 2026`（证书有效期前提，正常） |
| 内存 | `MemTotal: 36,240 kB`（约 35 MB）；`zkgui` VSZ ≈142 m → 与调研 §5.1 一致 |
| 用 **该 CA 文件**做 TLS 校验（主机侧，强制 TLS1.2） | `api.live.bilibili.com`、`live.bilibili.com` 均 **TLSv1.2 `ECDHE-RSA-CHACHA20-POLY1305`，verify=True** |

结论：**B 站设备直连的 TLS/CA 前提成立**（库 + 145 张根证书 + 设备时钟均可用）。
未验证（超出本任务边界）：设备内实际 TLS 握手、`getH5InfoByRoom` 4 KB JSON 解析与
`live_status/online/likes/fans` 映射——需 device-v3 代码在真机联调（本任务只读，不写设备/不刷机）。
全程未读取账号/cookie/SESSDATA，未改 `/data`。

## 10. 快手 Playwright 边界

- 本机 `importlib.util.find_spec("playwright") → None`；`/health` 的 `platforms.kuaishou = "requires_playwright"`。
- `GET /live?rooms=kuaishou:3xwnf96yetb5ewm` → 200，**166 B**，`ok:false, error:"kuaishou_requires_browser"`，
  耗时 2 ms、日志 `fetch failed ... in 0ms` → **未发任何网络请求**（代码 `has_playwright` 先判断，符合文档 §3.3）。
- 浏览器抓取路径（`KuaishouBrowser` + `KS_JS`：`window.__INITIAL_STATE__` / DOM 文本兜底 / `__NS_hxfalcon` 不逆向）
  本次**未实测**——与文档 §3.3/§9.4“本机未装 Chromium，未实测”一致。
- 边界：需 `pip install playwright && python -m playwright install chromium`；首次打开约 3–10 s，
  超过 2.5 s 预算 → 第一次轮询 `timeout`、后台完成后下一次有数据；常驻 Chromium 约 200–400 MB 内存，
  **不要放 OpenWrt 小路由器**（调研 §6.8），放 PC/NAS/树莓派 4+。
- 若 v3 一期只做 B 站（直连）+ 抖音（bridge），快手槽位显示 `--` 即可，不影响其他平台。

## 11. 设备接入建议（v3.0）

设备端配置（网页 `/api/config`，与契约 §2 一致）：

```json
"live": {
  "source": "hybrid",
  "url": "http://192.168.31.66:8081/live",
  "refreshSec": 30,
  "timeoutMs": 3000
}
```

- `hybrid` = **B 站设备直连 HTTPS + 抖音/快手走 bridge**（契约 §2/§5；B 站直连失败可回退 bridge，
  本 bridge 的 B 站实现保留、字段与抖音/快手同构）。
- `url` 必须是 `http://`（v3.0 设备不支持 https）、端口与 `--port` 一致、用**主机局域网 IP**
  `192.168.31.66`（bridge 启动日志会打印；不要写 127.0.0.1）。
- `refreshSec=30`：一页 2 个房间 → 2 请求/30 s；B 站最小间隔 10 s、抖音/快手 60 s 均满足；
  若抖音房间多，优先 19 位 `room_id` 走 `reflow`（另一套风控、无需 ttwid）。
- `timeoutMs=3000` > bridge 默认响应预算 2500 ms（余量 500 ms）；若想设更小，
  同步把 bridge `--deadline-ms` 设为 `timeoutMs - 300`（文档 §2.2）。
- 部署步骤：
  1. `cd tools/live-bridge && python -m pip install -r requirements.txt`；
  2. **默认绑定启动**：`python bridge.py --host 0.0.0.0 --port 8081`（或按 §7 的 systemd/NSSM 常驻）；
  3. 防火墙放行 **私有网络** TCP 8081（§5.1，需要管理员）；
  4. 确认 PC 与 TC002 同网段/VLAN，关闭 AP 隔离；
  5. 从另一台局域网机器自测：`curl "http://192.168.31.66:8081/live?rooms=bilibili:6,douyin:7076284313080843019"`
     应返回 200、~407 B、顶层 `ok:true`；
  6. 设备配置写入后，用 `GET /api/live/status`（设备 API）确认 `lastOk/lastError`；
  7. 旋钮按键/切页应触发立即刷新（bridge 无状态，重发请求即可）。
- 安全：只开局域网、**不要**端口映射到公网（协议无鉴权）；v3 契约无 token 字段。

## 12. 风险清单

| 级别 | 风险 | 依据 / 缓解 |
|---|---|---|
| P2 | **抖音匿名路径受 IP 风控**（200+0 字节），12 位 `web_rid` 尤其不稳；本次 1 次成功不代表长期可用 | 文档 §3.2/§9；建议抖音房间 ≤2、`refreshSec`≥30、优先 19 位 `room_id`；bridge 有 180 s 熔断、不猛冲 |
| P2 | **B 站 h5 内部接口可能变更**（`getInfoByRoom` 已 -352 先例），失效→降级 `get_info` 丢 `likes` | 文档 §3.1/§10；bridge 已实现端点健康降级 |
| P2 | **快手必须 Playwright**（本机未装，路径未实测）；首轮可能 timeout、浏览器占 200–400 MB | 文档 §3.3；一期可先显示 `--`，不阻塞 v3 |
| P3 | HTTP/1.0 请求收到 **HTTP/1.1 响应行** | 文档未承诺版本；设备 v3 `HttpClient` 只校验 `HTTP/` 前缀 + Content-Length（只读抽查），兼容；建议真机跑一次 |
| P3 | **Windows 上重复实例可同时监听 8081**（SO_REUSEADDR） | §8 D5；部署前用 netstat 检查；不要重复启动 |
| P3 | bridge 常驻内存 ~47 MB（非文档“十几 MB”） | §8 D3；小内存 NAS 需预留 |
| P3 | 明文 + 无鉴权，仅可信局域网 | 文档 §6/§10；不映射公网 |
| P3 | 4 KB 上限：2 房间远低于上限（407 B），但未来一页 4 房间+长标题时接近上限 | 契约 §1.2 与 `test_payload_capped_at_4kb`；必要时 `--title-max 0` |
| P3 | 线上 `rate_limited`/`timeout`/`network`/`too_large` 分支未主动触发 | 单测覆盖 0 字节限流、deadline 超时/旧值；线上未制造限流（避免烧 IP 配额） |
| P3 | 设备端 B 站直连只验证了 TLS/CA 前提，未在设备内跑通解析 | 需 device-v3/QA 真机联调（本任务边界外） |

无 P0/P1：bridge 对“一页 2 房间、30 s 轮询、B 站+抖音混排”的 v3.0 数据面已满足契约。

## 13. 复现命令（可直接跑）

```powershell
# 1) 离线单测（40 tests）
cd D:\Project\ulanzi\tools\live-bridge
python -m unittest -v test_bridge

# 2) 启动（本机验证用回环；设备接入用 --host 0.0.0.0）
python bridge.py --host 127.0.0.1 --port 8081 --log-level INFO

# 3) 健康检查 / 契约响应
curl.exe "http://127.0.0.1:8081/health"
curl.exe "http://127.0.0.1:8081/live?rooms=bilibili:6"
curl.exe "http://127.0.0.1:8081/live?rooms=bilibili:6,douyin:7076284313080843019"
curl.exe "http://127.0.0.1:8081/live?rooms=bilibili:3,bilibili:21452505"
curl.exe "http://127.0.0.1:8081/live?rooms=bilibili:999999999999"
curl.exe "http://127.0.0.1:8081/live?rooms=kuaishou:3xwnf96yetb5ewm"

# 4) 错误路径
curl.exe "http://127.0.0.1:8081/live"
curl.exe "http://127.0.0.1:8081/live?rooms=bilibili:6;rm"
curl.exe "http://127.0.0.1:8081/live?rooms=bilibili:1,bilibili:2,bilibili:3,bilibili:4,bilibili:5,bilibili:6,bilibili:7,bilibili:8,bilibili:9"
curl.exe "http://127.0.0.1:8081/nope"
curl.exe -X POST "http://127.0.0.1:8081/live"

# 5) 设备报文形状
curl.exe --http1.0 -H "Accept: application/json" -H "Connection: close" -D - "http://127.0.0.1:8081/live?rooms=bilibili:6"

# 6) --once / 退出码
python bridge.py --once --rooms bilibili:6                     # exit 0
python bridge.py --once --rooms kuaishou:3xwnf96yetb5ewm       # exit 2
python bridge.py --once --rooms "bilibili:6;rm"                # exit 2, bad_room_key
python bridge.py --version                                     # bridge.py 3.0.0

# 7) 设备只读边界（不写设备；完成后 disconnect）
$adb = "D:\Project\ulanzi\tools\platform-tools\adb.exe"
& $adb connect 192.168.31.124:5555
& $adb -s 192.168.31.124:5555 shell "ls -l /lib/libssl* /lib/libcrypto* /res/ui/cacert.pem; date"
& $adb disconnect 192.168.31.124:5555
```

## 14. 交付范围声明

- 新增：仅本文件 `docs/VERIFY-bridge.md`。
- 未改：`tools/live-bridge/bridge.py`（SHA-256 已记录）、`test_bridge.py`、固件源码、`docs/LIVE-BRIDGE.md` 等；
  未 `git commit`；未刷机/未部署设备/未重启设备；未使用任何账号、cookie、SESSDATA；
  测试临时文件（响应 JSON/头/脚本）只在 `%TEMP%\bridge-verify\`，未进入仓库。
- 设备侧只读操作：`adb connect/ls/pull/date`，已断开连接。
