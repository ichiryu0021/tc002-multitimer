# TC002 MultiTimer v3 实现契约（页面系统 + 直播监控）

> 状态：**冻结**（2026-10-22 19:20）。实现以此文件为准；三份设计文档
> `DESIGN-pages-v3.md` / `DESIGN-web-v3.md` / `RESEARCH-live-apis.md` 作为细节参考，
> 冲突处听本文件。v2.1.2 已固化，19:25–20:35 不刷机。

## 1. 目标（用户需求映射）

1. 旋钮切页；页面可在网页编辑：时钟、时钟+倒计时（上下分屏）、倒计时、直播。
2. 直播页一页 2 个直播间（上下两行）；可编辑多页；每行可选平台/房间/指标。
3. 直播数据平台：B站/抖音/快手。设备系统 `/lib/libssl.so.1.1` 的 TLS client 不可用；
   镜像在 `/res/lib` 携带 OpenSSL 1.1.1w，TlsClient 优先加载，**B站设备直连可用**；
   抖音/快手走局域网 bridge（明文 HTTP）。`push` 模式保留。
4. 倒计时仍严格绑定时钟：每帧只取一次 `time(NULL)`，所有视图共用同一个 `now`。
5. 多 WiFi、SoftAP、每分钟 NTP、自定义 NTP 等 v2.1.2 功能不得回归；设备 IP 由
   WiFi DHCP 自动获取，网页不再保存 IP 列表。

## 2. 配置 schema（`/data/multitimer.conf`，version 3）

```json
{
  "version": 3,
  "tzOffsetMinutes": 480,
  "warnSeconds": 10,
  "ntpServer": "",
  "colors": {"clock":"#FFFFFF","countdown":"#00FF00","countup":"#00BFFF","warning":"#FF2020","dim":"#808080"},
  "display": {"splitBottomWhenIdle":"date"},
  "targets": [{"date":"2026-10-23","time":"19:25:00","enabled":true}],
  "live": {"source":"hybrid","url":"http://192.168.31.10:8081/live","refreshSec":30,"timeoutMs":3000},
  "defaultPageId": "p0",
  "pages": [
    {"id":"p0","type":"clock","title":"时钟","shortTitle":"CLOCK","enabled":true},
    {"id":"p1","type":"clock_countdown","title":"时钟+倒计时","shortTitle":"CLOCK+CD","enabled":true},
    {"id":"p2","type":"countdown","title":"倒计时","shortTitle":"COUNTDOWN","enabled":true},
    {"id":"p3","type":"live","title":"直播","shortTitle":"LIVE","enabled":true,
     "metric":"online",
     "rooms":[
       {"platform":"bilibili","room":"","label":"","enabled":true,"metric":""},
       {"platform":"douyin","room":"","label":"","enabled":true,"metric":""}
     ]}
  ]
}
```

规则：

| 字段 | 规则 |
|---|---|
| `version` | 必须 3（POST 只收 3；v2 返回 400 提示刷新页面） |
| `tzOffsetMinutes` | -720..840，必填 |
| `warnSeconds` | 0..600，默认 10 |
| `ntpServer` | 空或 host/host:port，≤64 |
| `colors` | 缺省按上表；`dim` 默认 `#808080`；`countup` 默认 `#00BFFF` |
| `display.splitBottomWhenIdle` | **已废弃/网页不再提供**；保留兼容，渲染始终按“无目标显示日期”处理 |
| `targets` | ≤12，单项非法丢弃；超 12 → 400 |
| `live.source` | `"off"` / `"bridge"` / `"hybrid"` / `"push"`，默认 `"off"`；`hybrid` 下 B站走设备直连（镜像带 OpenSSL 1.1.1w），抖音/快手走 bridge |
| `live.url` | source=bridge/hybrid 时的中继地址（抖音/快手用；B站直连失败时兜底）；只接受 `http://`，≤160，无空格 |
| `live.refreshSec` | 15..300，默认 30 |
| `live.timeoutMs` | 1000..5000，默认 3000 |
| `pages` | 1..12，至少 1 项；未知 type → 400 |
| `pages[].id` | `[a-z0-9_-]{1,8}`，唯一；非法/重复自动生成 |
| `pages[].type` | `clock` / `clock_countdown` / `countdown` / `live` / `datetime` |
| `pages[].title` | ≤16 字符，可中文，仅网页/API 用，不上屏 |
| `pages[].shortTitle` | ≤8 可打印 ASCII，切页横幅用；空则 type 名 |
| `pages[].enabled` | 默认 true；旋钮跳过 disabled |
| `pages[].metric` | live 页默认指标，见下表；默认 `online` |
| `pages[].rooms` | live 页 1..2 项（**一页两个直播间**）；每项 `platform` / `room`(≤40) / `label`(≤3 ASCII) / `enabled` / `metric`(空=页默认) |
| `defaultPageId` | 必须引用存在页面；非法回退第一页 |

指标枚举：`online`（在线/人气）、`likes`（点赞）、`fans`（粉丝）、`status`（开播状态）、
`title`（标题，v3.0 不占用 2×2 格显示，保留给网页/推送）、`none`。v3.0 屏幕显示
`online/likes/fans/status`。

v2 → v3 迁移（`ConfigStore::load` 读磁盘时一次完成并立即保存）：

- 保留 `tzOffsetMinutes/warnSeconds/colors/targets/ntpServer`；新增 `dim`、`display`、`live`；
- `pages` 默认补 3 页：`p0 clock`、`p1 clock_countdown`、`p2 countdown`；
  `defaultPageId="p0"`；不自动创建 live 页；
- 首次全新安装（无配置）默认 4 页，追加 `p3 live`（2 个空槽，source=off）。

## 3. 页面渲染（52×16）

- `clock`：`HH:MM:SS` scale 2，`colors.clock`。
- `datetime`：时间 `HH:MM:SS` scale 2 放上半屏（baseline 10），日期 `YYYY-MM-DD`
  scale 1 放底部（baseline 15，dim）；用于单独看日期+时间。
- `clock_countdown`：上半 0..7px 时钟，下半 8..15px 倒计时；两半均 scale 1，
  基线 7 / 14；共用同一 `now`；时间行用大字号间距尽量占满 52px。
  **目标到达后显示正计时**（从最近过期目标累计，`colors.countup`）；**没有任何目标**
  时下半显示 `NO COUNTDOWN`（dim）。`display.splitBottomWhenIdle` 已废弃。
- `countdown`：有未来目标显示倒计时；目标到达后显示正计时（`colors.countup`）；
  **没有任何目标**显示 `NO COUNTDOWN`（dim），不再自动回时钟。日期请用 `datetime` 页。
- `live`：**上下两行**，每行一个直播间（一页 2 个）；行高 8px，TomThumb scale 1，
  基线 6 / 14；每行 `<label><gap><value>`：label ≤3 字符（默认平台字母+房间尾号，
  如 `B25`），value ≤6 字符（`1234`/`12.3w`/`9.9y`/`--`）；平台颜色：B `#FF6699`、
  D `#FFFFFF`、K `#FF9900`；value 白色；失败/未配置 `--` dim；整页无数据 `NO LIVE` dim。
- 预警：倒计时 ≤ `warnSeconds` 用 `colors.warning`，不闪烁。
- 覆盖层仍最高优先级；切页横幅次之（1.2s），然后页面内容。
- 所有时间格式 `HH:MM:SS`；日期 `YYYY-MM-DD`。

## 4. 页面宿主 / 旋钮 / 状态

- 单个 `PageHost`（重构 TimerPage）持有 `pages` 与当前下标；热应用 = 配置交换 + 下标 clamp。
- 旋钮 CW = 下一页，CCW = 上一页；跳过 `enabled=false`；默认循环；切页立即重绘并显示
  `shortTitle` + `i/N` 1.2s。
- 中键**单击**显示 IP/NO WIFI（全屏 scale2，超宽水平滚动，无“IP”前缀）；**右键长按 3s**
  进入/退出 SoftAP；旋钮按键 = 立即刷新直播；左键不分配。旋钮 CW = 下一页（正向），
  CCW = 上一页（反向）。
- 渲染节拍 250ms；只有秒变化/切页/横幅/覆盖层/直播数据变化才 `sendLedData`，
  稳态仍约 1 帧/秒。
- 当前页持久化：`StoragePreferences["uiState"]` = `"id,index,epoch"`；稳定 ≥30s 且
  距上次写 ≥120s 才落盘；恢复优先级 最后停留页 > `defaultPageId` > 第一页。
- 直播数据：`LiveService` 单后台线程；`hybrid` 下 B站走设备 HTTPS 直连（镜像
  `/res/lib` 携带 OpenSSL 1.1.1w，系统 1.1.0i 不可用），B站失败时回退 bridge；
  抖音/快手按页批量走 bridge HTTP。缓存 24 条，`refreshSec` 轮询，切页/旋钮按键立即刷新，
  超时/失败不阻塞 UI；UI 只取快照。
- 锁顺序：PageHost 锁内不调 LiveService 网络；LiveService 不回调 PageHost；网络 I/O 不持锁。

## 5. Bridge 协议（v3.0 设备端只做明文 HTTP）

设备请求：

```text
GET {url}?rooms=bilibili:21452505,douyin:88776655,kuaishou:3x8k2m9
Host: ...
Accept: application/json
Connection: close
```

响应：

```json
{"ok":true,"ts":1792658309,"rooms":[
  {"key":"bilibili:21452505","platform":"bilibili","room":"21452505",
   "ok":true,"online":12345,"likes":6789,"fans":9000000,"status":1,"title":"..."},
  {"key":"kuaishou:3x8k2m9","ok":false,"error":"signature"}
]}
```

- `ok:false` 或缺失 → 该格 `--`；`online` 等数值 0 是有效值，不要用 0 判断失败。
- 设备只接受 `http://` bridge；bridge 由 PC/NAS 提供，主要承担抖音/快手。
  B站由设备直连（镜像 `/res/lib` 的 OpenSSL 1.1.1w），失败时可回退 bridge。
- `push` 模式：`POST /api/live` body 同响应格式，HTTP 线程写入缓存。

## 6. HTTP API 变更

| 方法 | 路径 | 说明 |
|---|---|---|
| GET/POST | `/api/config` | v3；POST 只收 v3 |
| POST | `/api/page` | `{"id":"p1"}` 或 `{"index":1}` 切当前页，不写配置 |
| GET | `/api/live/status` | `{ok,source,lastOk,lastError,ts,rooms:[...capabilities]}` |
| POST | `/api/live/refresh` | 触发一次异步刷新，立即返回 |
| POST | `/api/live` | push 模式推数据 |
| GET | `/api/status` | 增加 `page:{id,index,title,type}`；其余保持 |
| GET/POST | `/api/wifi*`、`/api/time` | 保持 v2.1.2 |

请求体上限 8192→16384；错误仍 `{"ok":false,"error":"..."}`。

## 7. 文件所有权（并行开发）

| Agent | 独占文件 |
|---|---|
| core-v3 | `src/pages/PageTypes.h`、`PageRenderer.h/.cpp`、`src/live/LiveFormat.h/.cpp`、`src/config/ConfigStore.*`、`tests/test_page_renderer.cpp`、`tests/test_live_format.cpp`、`tests/test_config_store.cpp` |
| device-v3 | `src/pages/PageHost.h/.cpp`、`src/pages/TimerPage.*`、`src/live/LiveService.*`、`LiveCache.*`、`src/net/HttpClient.*`、`src/net/HttpServer.*`、`src/logic/mainLogic.cc`、`src/net/ConfigMode.*`（仅必要修正） |
| web-v3 | `web-src/index.html`、`native/MultiTimer/tools/check_web.js`、`src/web/editor_gz.h` |
| bridge-v3 | `tools/live-bridge/**`、`docs/LIVE-BRIDGE.md` |
| qa-v3 | 只读审查、测试脚本；不改源码 |

公共头由 core-v3 冻结；device/web 可先按本契约编码。

## 8. 验收

- 主机单测 ≥140（含页面像素、格式、v3 配置/迁移、LIVE 格式）；
- 网页自检 ≥80；390px 无横向溢出、0 console error；
- 真机：
  1. 旋钮切页，跳过停用页，顺序正确；
  2. 分屏页上下同一秒，倒计时跳时/改时区立即变化；
  3. live 页上下两行，2 个房间，`--`/数据/失败占位正确；
  4. bridge 轮询 30s，切页/旋钮按键刷新；
  5. v2 配置升级后页面/目标/时区/颜色/NTP/WiFi 保留；
  6. 19:25–20:35 之外才刷机。
