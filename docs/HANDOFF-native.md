# MultiTimer v3.3 原生方案交接记录

> 快照时间：2026-10-22 22:02:00（Asia/Shanghai，v3.3 源码/镜像/真机验证完成）
> 方案：Ulanzi TC002 / FlyThings 原生 C++（Z21）
> 状态：**v3.3 已实现并完成真机固化/验证：页面系统（含 NO COUNTDOWN / datetime）+ 正计时 + 直播监控；
> 单测 211/211、网页自检 90/90、交叉编译 0 errors / 0 warnings；
> QA 主机侧 P0=0/P1=0/P2=10（仅 v3.0 基线，v3.3 暂无独立 QA）；
> 最终源码产物 722,532 字节 / `1A495991...`（21:49:02，`build/v3g-build.log`）；
> `dist/update.img` 1,942,076 字节 / `8B2BCF20...`（21:49:28 生成）已刷入 `/res`，
> 回读 hash 一致、`/tmp` 空、`/data` 保留；
> **B 站设备直连已可用**（镜像 `/res/lib` 携带 OpenSSL 1.1.1w），
> 抖音/快手走 bridge；`hybrid` B 站直连失败回退 bridge；
> 真机证据见 §0/STATE-v3 §10**
> 工程：[`native/MultiTimer/`](../native/MultiTimer/)
> 实现契约：[`V3-CONTRACT.md`](V3-CONTRACT.md)（v3.2 语义已部分同步；v3.3 的 NO COUNTDOWN/datetime 尚未写入契约，以本文/源码为准）
> QA 报告（v3.0 基线）：[`QA-v3-initial.md`](QA-v3-initial.md)
> QA 最终回归（v3.0 基线）：[`QA-v3-final.md`](QA-v3-final.md)
> 设计稿：[`DESIGN-pages-v3.md`](DESIGN-pages-v3.md)、[`DESIGN-web-v3.md`](DESIGN-web-v3.md)
> 直播调研/中继：[`RESEARCH-live-apis.md`](RESEARCH-live-apis.md)、
> [`LIVE-BRIDGE.md`](LIVE-BRIDGE.md)、[`VERIFY-bridge.md`](VERIFY-bridge.md)
> 冻结快照：[`STATE-2026-10-22-v3.md`](STATE-2026-10-22-v3.md)
> 固化记录：[`FLASH-RUNBOOK.md`](FLASH-RUNBOOK.md)
> v2.1.2 历史快照：[`STATE-2026-10-22-v21.md`](STATE-2026-10-22-v21.md)

## 0. 先读：当前代码状态

- 唯一维护方案是 FlyThings 原生 App；旧的 AWTRIX NG / Berry / Python 方案已整体移到
  [`legacy/awtrix-ng/`](../legacy/awtrix-ng/)，不再维护。
- v3 的字段、语义、显示规则以 [`docs/V3-CONTRACT.md`](V3-CONTRACT.md) 为准；
  设计文档只作细节参考，冲突处听契约。
- **`dist/` 与设备 `/res` 已是 v3.3**：`dist/update.img` 1,942,076 字节 /
  `8B2BCF20...`（21:49:28 生成）已刷入；回读 `/res/lib/libzkgui.so` 722,532 字节 /
  `1A495991...`（与 `dist/lib/libzkgui.so` 一致）、`/tmp` 为空、`/data` 配置保留；
  真机证据见 [`STATE-2026-10-22-v3.md`](STATE-2026-10-22-v3.md) §10.2。
  v3.0（718,436 / `A4B1D7E8...`）与更早产物为历史。
- **B 站设备直连在 v3.3 已可用**：镜像在 `/res/lib/` 携带 OpenSSL 1.1.1w
  （Debian 11 armhf），`TlsClient` 优先 `dlopen` 它；系统自带 1.1.0i 无 TLS client
  method，只作回退。`hybrid` 下 B 站 HTTPS 直连，抖音/快手走 bridge；B 站直连失败
  且 `live.url` 非空时同轮回退 bridge 并 60 分钟退避。**真机证据：停止 bridge 后
  B 站示例房间仍 `state=ok`。**
- v3.3 新增/修改的主要文件：
  - `src/pages/PageTypes.h`（页面/直播/指标/数据源数据结构与上限常量）
  - `src/pages/PageRenderer.h/.cpp`（纯函数 52×16 渲染：5 种页面、正计时/NO COUNTDOWN/datetime、
    横幅、IP 滚动覆盖层）
  - `src/pages/PageHost.h/.cpp`（页表、翻页、横幅、覆盖层、IP 滚动偏移、单帧时间源、
    uiState 持久化、`mode=countup` 状态）
  - `src/pages/TimerPage.h/.cpp`（退化为 `PageHost` + `PageBase::sendLedData` 适配层）
  - `src/live/LiveTypes.h`、`LiveFormat.h/.cpp`（直播快照/格式化/平台色/label）
  - `src/live/LiveCache.h/.cpp`（≤24 条房间缓存、状态机，纯 C++）
  - `src/live/LiveService.h/.cpp`（单后台线程：B 站 HTTPS 直连 / 抖音快手 bridge 批量 /
    push；B 站失败回退 bridge + 60 分钟退避）
  - `src/net/HttpClient.h/.cpp`（设备端最小明文 HTTP 客户端，只接受 `http://`）
  - `src/net/TlsClient.h/.cpp`（`dlopen` OpenSSL：优先 `/res/lib` 的 1.1.1w，
    CA `/res/ui/cacert.pem`）
  - `res-extra/lib/libssl.so.1.1`、`libcrypto.so.1.1`（镜像新增的 OpenSSL 1.1.1w，
    由 `make-image.ps1` 叠加到 `/res/lib`）
  - `third_party/openssl-armhf/`（来源/许可/兼容性说明）
  - `src/net/BilibiliClient.h/.cpp`、`BilibiliParse.h/.cpp`（B 站 h5 主端点 + get_info 兜底）
  - `src/config/ConfigStore.*`（v3 schema、v2/v1→v3 迁移、只收 v3；`colors.countup` 缺省补默认）
  - `src/net/HttpServer.*`（v3 API：`/api/page`、`/api/live*`、`/api/status.page`、16 KB 体限）
  - `src/logic/mainLogic.cc`（250ms 渲染节拍、按键路由、LiveService/PageHost 启动）
  - `web-src/index.html`（v3 页面/直播编辑器 + 数据源 + v3 模拟器）、
    `tools/check_web.js`（90 项自检）、`src/web/editor_gz.h`（重新生成的内嵌 gzip）
- 真机历史证据（v2.1.2 仍有效）：设备 `192.168.31.124`，Z21，官方固件 1.1.3、
  MCU V1.0.17；v2.1.2 于 19:06:00 左右通过官方 ADB 完成 `/res` 固化，`/tmp` 为空、
  `/data` 保留、`GET /api/status` 200、80 端口 302 到 `:8080`。
  目标配置由用户维护，本文不对具体 `targets` 内容作断言。
- v3.3 镜像已于 2026-10-22 21:49:28 生成并完成 `/res` 固化与真机验证；
  验证用临时目标已删除、用户原有配置已恢复（具体以设备 `GET /api/config` 为准）。

## 1. 需求

### 1.1 v2（基础，语义不变）

为 Ulanzi TC002（52×16 RGB，Z21 平台）提供设备独立运行的时钟 + 多目标倒计时：

- 正常时钟：无未来目标时显示本地 `HH:MM:SS`；
- 带日期多目标：`YYYY-MM-DD` + `HH:MM:SS`，最多 12 个，按时间升序、过期跳过；
- 严格绑定时钟：每秒重新取系统时钟计算剩余秒数，不缓存递减；
- 到点显示 `00:00:00`，下一秒切下一个未来目标，无目标回时钟；
- 预警（≤ `warnSeconds` 常亮红，不闪烁）、远目标（≥100h）`DdHH:MM:SS`；
- 时区分钟偏移 -720..840，不依赖 tzdata、不自动 DST；
- 掉电保存配置与时钟；无 RTC 时用最后保存时刻恢复；
- 手机浏览器编辑页面由设备内置 HTTP 服务托管；固化后开机直接运行。

### 1.2 v2.1（保留，不得回归）

- 多 WiFi 网络列表（≤8）、按顺序兜底、网页增删/启停/排序/编辑；
- SoftAP 配网（**v2.1.2 入口=长按中键；v3.3 改为右键长按 3s**，SSID
  `TC002-Setup`、固定密码、AP IP `192.168.100.1`、15 分钟超时、AP/STA 互斥）；
- 每分钟 NTP（每 60s 一次、每 30s 检查节流）、自定义 NTP 服务器优先/回退内置；
- 网页多时钟地址（v2.1.2 行为；v3.3 已移除，改为当前 origin 自动地址）；
- 80→8080 跳转常开（80 绑定失败只告警）。

### 1.3 v3 / v3.3（本轮新增）

1. **页面系统**：`pages[]` 可配置（1..12 页），旋钮 CW/CCW 翻页、跳过停用页、
   首尾循环、1.2s 切页横幅；网页可视化编辑页面列表与顺序。
2. **五类页面**：`clock`、`clock_countdown`（上下 8+8 分屏，时钟与倒计时同秒）、
   `countdown`、`live`（**上下两行，一页 2 个直播间**，可多页）、
   `datetime`（日期 + 时间：时间大字 + 日期小字）。
3. **正计时 / NO COUNTDOWN**（v3.3）：目标到达后从最近的过期目标累计
   （`colors.countup` 默认 `#00BFFF`）；完全无有效目标显示 `NO COUNTDOWN`（dim），
   不再回时钟、不再显示日期。
4. **直播监控**：B 站设备 HTTPS 直连（镜像携带 OpenSSL 1.1.1w）+ 抖音/快手局域网
   bridge（明文 HTTP）；保留 `push`；`live.source` = `off`/`bridge`/`hybrid`/`push`；
   `hybrid` 下 B 站直连、失败回退 bridge；指标 `online`/`likes`/`fans`/`status`；
   平台/房间/指标/刷新参数可在网页调整。
5. **配置 v3**：`version:3`、`pages`、`live`、`colors.dim`/**`colors.countup`**；
   `display` 保留兼容但已废弃（网页 UI 移除、渲染忽略 blank）；v2/v1 自动迁移；
   `POST /api/config` 只收 v3。
6. **API v3**：`/api/page`、`/api/live/status`、`/api/live/refresh`、`/api/live`（push）；
   `/api/status` 增加 `page`/`text2`/`banner`/`overlay`，`mode` 增加 `countup`；
   datetime 页 `text=HH:MM:SS`/`text2=YYYY-MM-DD`；无目标时 `NO COUNTDOWN`；
   保留 WiFi/NTP/time/80 跳转。
7. **按键/IP**：中键单击显示全屏 IP（无前缀、超宽滚动、12s）；右键长按 3s 进出
   SoftAP；旋钮翻页/按键刷新不变。
8. **当前页持久化**：`StoragePreferences["uiState"]` 节流落盘；重启恢复最后停留页。
9. **网页 v3.3**：页面列表/编辑、直播 2 槽位、指标联动、全局直播数据源、模拟器
   （分屏/两行/正计时帧/datetime 帧）、正计时颜色、地址自动（origin，自动补 :8080）、
   页面类型下拉含“日期 + 时间”、多 WiFi、NTP 保留；不再保存 IP 地址列表。
   （网页无内置直播监控卡；直播状态走 `GET /api/live/status`。）

明确不做：响铃、闪烁、每日循环、自动 DST、OTA、MQTT、鉴权、WiFi 主动扫描、
AP 下同时 STA、HTTP 配网开关、自定义跑马灯（IP 滚动除外）。

## 2. 文件清单（v3）

| 文件 | 作用 |
|---|---|
| `native/MultiTimer/Manifest.xml` | Z21 平台与依赖包声明 |
| `native/MultiTimer/ui/main.ftu` | 空白主 Activity（页面由 PageManager 自绘） |
| `src/Main.cpp`、`src/activity/` | EasyUI 入口 / IDE 自动生成（勿手改） |
| `src/logic/mainLogic.cc` | 启动编排：防砖、MCU、时钟恢复、配置/迁移、PageHost、按键、WiFi、HTTP、LiveService；250ms 定时器 |
| `src/timer/TimerTypes.h` | v3 `TimerConfig`（v2 字段 + `colorCountup`/`colorDim`/`splitBottomWhenIdle`/`live`/`defaultPageId`/`pages`），include `pages/PageTypes.h` |
| `src/timer/TimeMath.*`、`TimerEngine.*` | 日历/时区/格式化、目标排序去重/阶段/预警 |
| `src/time/TimeKeeper.*` | 系统时钟、恢复、有效性、300s 持久化（平台层） |
| `src/pages/PageTypes.h` | 页面类型、平台、指标、数据源枚举；上限常量（12 页、每页 2 房间等） |
| `src/pages/PageRenderer.*` | 纯函数渲染：clock / clock_countdown / countdown / live / datetime / 正计时与 NO COUNTDOWN / 横幅 / IP 滚动覆盖层；TomThumb 度量 |
| `src/pages/PageHost.*` | 页表、当前页、翻页、横幅、覆盖层（含 IP 滚动偏移）、`tick()` 单帧时间源、状态快照（countup/NO COUNTDOWN/datetime）、uiState |
| `src/pages/TimerPage.*` | 注册名 `TimerPage`，只做 PageHost + `PageBase::sendLedData` 适配 |
| `src/pages/PageBase.*` | `sendLedData()`：52×16 RGB → 64×16 SPI 帧 |
| `src/live/LiveTypes.h` | `LiveSnapshot`/`LiveRoomData`/`LiveMetricValue`（0 与缺失严格区分） |
| `src/live/LiveFormat.*` | 指标格式化（`w`/`y`）、`status` → `LIVE`/`OFF`、平台色、label 生成 |
| `src/live/LiveCache.*` | 房间缓存（≤24）、`empty/wait/ok/stale/error` 状态计算，纯 C++ |
| `src/live/LiveService.*` | 单后台线程：B 站 HTTPS 直连 / 抖音快手 bridge 轮询 / push、缓存写入、`/api/live/status` 数据 |
| `src/net/HttpClient.*` | 明文 HTTP GET（`http://` only、超时、chunked/Content-Length） |
| `src/net/TlsClient.*` | `dlopen` OpenSSL（优先 `/res/lib` 的 1.1.1w）+ `/res/ui/cacert.pem` 的 HTTPS GET |
| `src/net/BilibiliClient.*`、`BilibiliParse.*` | B 站 h5 主端点 + `get_info` 兜底、纯逻辑解析/校验/HTTP 解码 |
| `src/net/HttpServer.*` | v3 HTTP 服务（8080 + 80 跳转）、内嵌手机页、全部 API |
| `src/net/ConfigMode.*` | SoftAP 配网（右键长按入口，v2.1 语义其余不变） |
| `src/net/WifiManager_task.*`、`SntpClient.*` | WiFi 自举/列表尝试/每分钟 SNTP；自定义服务器 |
| `src/net/mini_json.*` | 无第三方依赖 JSON |
| `src/config/ConfigStore.*` | v3 校验/原子保存、v1/v2→v3 迁移、`colors.countup` 缺省补默认、`toJson`（字段顺序稳定） |
| `src/config/WifiStore.*` | WiFi v2 多网络列表（≤8）、旧单对象兼容 |
| `src/managers/`、`mcuProtocol/`、`uart/`、`utils/` | MCU/按键/页面管理、串口协议、Surface/Painter 等 |
| `src/Fonts/TomThumb.h` | 5×8 点阵字体 |
| `src/web/editor_gz.h` | 内嵌手机页 gzip 字节（**构建产物**，勿手改） |
| `web-src/index.html` | 手机编辑页源文件（单文件零外链，页面/直播编辑 + 数据源 + 模拟器 + 正计时颜色 + 自动地址） |
| `native/MultiTimer/tools/build_web_asset.py` | `index.html` → `editor_gz.h` |
| `native/MultiTimer/tools/check_web.js` | 网页逻辑自检（90 项） |
| `tools/live-bridge/**` | 直播数据中继（Python 3，B 站/抖音/快手；文档 `docs/LIVE-BRIDGE.md`） |
| `tools/build.ps1` | Windows headless 构建 |
| `tools/make-image.ps1` / `flash-update.ps1` / `deploy-tmp.ps1` | 镜像生成 / 官方 ADB 固化（默认 DryRun）/ `/tmp` 部署 |
| `tests/` | 主机单测 211 用例 + 网页自检 |
| `docs/V3-CONTRACT.md` | v3 实现契约（冻结） |
| `docs/DESIGN-pages-v3.md`、`docs/DESIGN-web-v3.md` | 页面/网页设计稿 |
| `docs/RESEARCH-live-apis.md` | 直播接口调研（2026-10-22 实测） |
| `docs/LIVE-BRIDGE.md`、`docs/VERIFY-bridge.md` | bridge 协议/部署、实测记录 |
| `docs/HANDOFF-native.md` | 本文件 |
| `docs/STATE-2026-10-22-v3.md` | v3.3 工作快照（当前） |
| `docs/FLASH-RUNBOOK.md` | `/res` 固化执行记录、验证与回滚 |
| `docs/STATE-2026-10-22-v21.md`、`-v2.md`、`.md` | v2.1.2 / v2 / v1 历史快照（保留不动） |

## 3. 启动与数据流

```text
mainActivity
 └─ onUI_init()
     ├─ SystemProperties::setString("sys.zkapp.state", "running")  // 防砖，必须最早
     ├─ McuManager::initialize("/dev/ttyS1", 1500000) + queryMcuVersion()
     ├─ TimeKeeper::restore()                        // 页面/配置前
     ├─ 注册 TimerPage
     ├─ TimerPage::restoreUiState()                  // 读 StoragePreferences["uiState"]
     ├─ ConfigStore::load(config, now, &migrated)
     │    ├─ 成功 → TimerPage::setConfig()（PageHost 页表/Engine）+
     │    │         SntpClient::setServer(config.ntpServer) + LiveService::setConfig()
     │    ├─ migrated → ConfigStore::save()（v3，只写一次）
     │    └─ now 无效且疑似 v1 → 暂缓，时间有效后补迁移一次
     ├─ LiveService::start()                         // 单后台线程（常驻）
     ├─ PageManager::navigateTo("TimerPage")
     ├─ KeyManager：中键单击 IP/NO WIFI、右键长按 3s SoftAP；
     │              旋钮 CW/CCW 翻页；旋钮按键刷新直播；左键不分配
     ├─ ConfigMode::setOnReady / WifiBootstrap::setOnConnected（覆盖层回调）
     ├─ HttpServer::start(8080, applyCallback)       // 内部再试监听 80 做 302
     └─ startWifiBootstrap()                         // 非阻塞后台

每 250ms（onUI_Timer，id=1）：
  TimerPage::tick() → PageHost::tick(monotonicMs)
      ├─ 本帧唯一一次 time(NULL) → TimerEngine::evaluate(now)（严格绑定时钟）
      ├─ live 页：解锁后取 LiveService::snapshotForPage()（PageHost 锁外）
      ├─ 脏帧判定（秒变化/切页/横幅/覆盖层/直播数据哈希），需要才绘制
      ├─ PageRenderer::renderPage / renderPageBanner / renderOverlayTextScrolled → 52×16
      ├─ TimerPage → PageBase::sendLedData()（补到 64 列后 SPI 发送）
      └─ persistTick()：uiState 稳定 ≥30s 且距上次写 ≥120s 才落盘
每 4 tick（=1s）：TimeKeeper::tick()（300s 落盘）、ConfigMode::tick()、
                  时间有效后补 v1 迁移、右键长按检测
```

- LiveService 与 UI 完全解耦：后台线程 B 站 HTTPS 直连 / 抖音快手 bridge 轮询，
  缓存由锁保护，UI 只取快照；
  网络 I/O 不持锁，不在 UI 线程做 socket。
- `POST /api/config` 的 `applyCallback` 内只做 `SntpClient::setServer`、
  `PageHost::setConfig`（含页下标 clamp）、`LiveService::setConfig/noteActivePage`，
  不做阻塞 I/O。

### 关键类型与接口（v3）

```cpp
// pages/PageTypes.h
enum PageType { PAGE_TYPE_CLOCK, PAGE_TYPE_CLOCK_COUNTDOWN,
                PAGE_TYPE_COUNTDOWN, PAGE_TYPE_LIVE };
enum LivePlatform { LIVE_PLATFORM_BILIBILI, LIVE_PLATFORM_DOUYIN,
                    LIVE_PLATFORM_KUAISHOU, /* huya/douyu/custom 扩展位 */ };
enum LiveMetric { LIVE_METRIC_INHERIT = -1, LIVE_METRIC_NONE, LIVE_METRIC_ONLINE,
                  LIVE_METRIC_LIKES, LIVE_METRIC_FANS, LIVE_METRIC_STATUS,
                  LIVE_METRIC_TITLE };
enum LiveSourceKind { LIVE_SOURCE_OFF, LIVE_SOURCE_BRIDGE,
                      LIVE_SOURCE_HYBRID, LIVE_SOURCE_PUSH };
const int kMaxPageCount = 12;
const int kMaxRoomsPerLivePage = 2;    // 一页两个直播间
const int kMaxRoomChars = 40;
const int kMaxRoomLabelChars = 3;
const int kMaxLiveValueChars = 6;
const int kMinLiveRefreshSec = 15, kMaxLiveRefreshSec = 300,
          kDefaultLiveRefreshSec = 30;
const int kMinLiveTimeoutMs = 1000, kMaxLiveTimeoutMs = 5000,
          kDefaultLiveTimeoutMs = 3000;

struct LiveRoomConfig { LivePlatform platform; std::string room, label;
                        bool enabled; LiveMetric metric; };
struct PageConfig { std::string id, title, shortTitle; PageType type;
                    bool enabled; LiveMetric metric;
                    std::vector<LiveRoomConfig> rooms; };
struct LiveSourceConfig { LiveSourceKind kind; std::string url;
                          int refreshSec, timeoutMs; };

// timer/TimerTypes.h
struct TimerConfig {
    int tzOffsetMinutes;              // -720..840
    int warnSeconds;                  // 0..600
    uint32_t colorClock, colorCountdown, colorWarning, colorDim;  // v3 dim
    std::string ntpServer;            // 空 = 内置列表
    SplitIdle splitBottomWhenIdle;    // date / blank
    LiveSourceConfig live;            // v3
    std::string defaultPageId;        // v3
    std::vector<PageConfig> pages;    // v3，1..12
    std::vector<DateTarget> targets;  // 最多 12
};

// pages/PageHost.h（UI 线程）
struct PageStatus { /* v2 字段 + text2/banner/pageId/pageIndex/pageCount/
                       pageType/pageTitle（v3） */ };
class PageHost {
    void setConfig(const TimerConfig&);      // 热应用 + 下标 clamp
    void restoreUiState(); void resetUiState();
    void showOverlay(const std::string&, uint32_t rgb, uint32_t durationMs);
    bool tick(long long monotonicMs, Surface&); // 脏帧才返回 true
    bool nextPage(); bool prevPage(); bool goToPage(int);
    bool goToPageId(const std::string&); bool requestLiveRefresh();
    bool onKeyEvent(int keyCode, int keyStatus);
    bool getStatus(PageStatus&) const;
    static const int kBannerDurationMs = 1200;
    static const int kStablePersistMs = 30000;
    static const int kMinPersistIntervalMs = 120000;
};

// pages/TimerPage.h：注册名仍 "TimerPage"，转发到 PageHost + sendLedData
// pages/PageRenderer.h（纯函数，不读全局时间）
bool renderPage(const TimerConfig&, int pageIndex, long long nowUtc,
                const Stage&, bool timeValid, const LiveSnapshot&, Surface&);
void renderPageBanner(int index, int count, const std::string& shortTitle,
                      const TimerConfig&, Surface&);
void renderOverlayText(const std::string& text, uint32_t rgb, Surface&);

// live/LiveService.h（单后台线程）
class LiveService {
    static LiveService& getInstance();
    void start(); void shutdown(); bool isStarted() const;
    void setConfig(const TimerConfig&);       // 派生数据源 + 每页房间表
    void noteActivePage(int pageIndex);       // 切页置 urgent
    void requestRefresh(int pageIndex = -1);
    void refreshNow();
    bool snapshotForPage(int pageIndex, LiveSnapshot&) const;
    bool statusJson(JsonValue&) const;        // GET /api/live/status
    bool pushJson(const JsonValue&, int& updated, std::string& error);
};

// net/HttpClient.h：设备端最小明文 HTTP（只接受 http://）
class HttpClient {
    static bool parseUrl(const std::string&, std::string& host, int& port,
                         std::string& path, std::string& error);
    static bool get(const std::string& url, int timeoutMs,
                    size_t maxBodyBytes, HttpClientResponse&);
};
// net/TlsClient.h：dlopen libssl/libcrypto，CA=/res/ui/cacert.pem
class TlsClient {
    static bool get(const std::string& host, const std::string& path,
                    TlsHttpResponse&, int timeoutMs = 4000);
    static bool available(std::string& error);
};
// net/BilibiliClient.h
class BilibiliClient {
    static bool fetchRoom(const std::string& roomId, BilibiliRoomData& out,
                          int timeoutMs = 4000);
};

// config/ConfigStore.h（v3）
class ConfigStore {
    static const int kConfigVersion = 3;
    static const int kMaxTargets = 12;
    static const int kMaxPages = kMaxPageCount;
    static const int kMaxRoomsPerLivePage = 2;
    static TimerConfig defaultConfig();   // 新装 4 页（含 live 页）
    static bool load(TimerConfig&, long long nowUtc, bool* migrated = NULL);
    static bool save(const TimerConfig&);
    static std::string toJson(const TimerConfig&);
    static bool fromJson(const JsonValue&, TimerConfig&, std::string& err); // 只收 v3
};

// 保持 v2.1.2 接口：TimeKeeper / WifiStore / ConfigMode / SntpClient /
// HttpServer::start(int, ApplyCallback)
```

## 4. 时间与显示语义

### 4.1 时间（v2 语义不变）

- 设备无 RTC、无 tzdata，系统时区固定 UTC（`sysclktz 0`）；
  本地时间 = UTC + `tzOffsetMinutes` 分钟（-720..840，不自动 DST）。
- 有效时间：`now >= 1700000000`（2023-11-14）；无效显示 `--:--:--`。
- 目标存本地日期 + 本地墙上时间；改时区后按新时区换算目标 epoch。
- `TimeKeeper::restore()`：系统时间有效则只恢复 `lastSync`；无效且 flash 有可用
  epoch 则 `settimeofday()` 恢复（不当作真实对时）；都没有则等 SNTP/网页对时。
- 持久化：`StoragePreferences["clockState"]="epoch,lastSync"`；
  `setUtc()` 立即保存；SNTP 成功只 `noteSynced()` 更新内存，`tick()` 每 300s 落盘；
  首次从无效变有效立即保存；无效时间不落盘。
- SNTP：链路建立后立即尝试；在线每 30s 检查节流、距上次成功 ≥60s 才发下一轮
  （最多每分钟一次）；自定义 `ntpServer` 优先，失败回退内置
  `ntp.aliyun.com`/`cn.pool.ntp.org`/`pool.ntp.org`/`time.cloudflare.com`；
  单服务器超时 2s；变化 >1 秒打 INFO，否则 DEBUG。
- 网页对时：加载时设备无效或与浏览器差 >2s 自动 `POST /api/time`；
  保存成功后静默对时一次；也可手动“立即对时”。
- **严格绑定**：`PageHost::tick()` 每帧只取一次 `time(NULL)`，`TimerEngine::evaluate(now)`
  与页面渲染共用同一个 `now`；分屏页上下两半、时钟与倒计时/正计时严格同秒。

### 4.2 显示

页面渲染以 [`V3-CONTRACT.md`](V3-CONTRACT.md) §3 与
[`STATE-2026-10-22-v3.md`](STATE-2026-10-22-v3.md) §3 为准，摘要：

| 页面 | 主文本 | 次文本 | 缩放/基线 |
|---|---|---|---|
| `clock` | 本地 `HH:MM:SS`；无效 `--:--:--` | — | scale 2，居中（基线 13） |
| `clock_countdown` | 上半本地 `HH:MM:SS`；无效 `--:--:--` | 下半倒计时 / **正计时**（`colors.countup`）/ `NO COUNTDOWN`（无有效目标，dim）/ 时间无效留空 | 均 scale 1，基线 7 / 14 |
| `countdown` | 倒计时 `<100h` scale 2；`>=100h` `DdHH:MM:SS` scale 1；**目标到达后正计时**（不再回时钟）；无有效目标显示 `NO COUNTDOWN`（dim）；无效 `--:--:--` | — | 同 v2 |
| `datetime` | 时间 `HH:MM:SS` scale 2（基线 10，字距 2→0 自适应）；日期 `YYYY-MM-DD` scale 1（基线 15） | — | 时间 `colors.clock`；日期 `colors.dim` |
| `live` | 上行 `<label><value>` | 下行 `<label><value>` | 均 scale 1，基线 6 / 14 |

- 预警：`warnSeconds > 0 && remain <= warnSeconds` → `colors.warning`，常亮不闪烁。
- 覆盖层最高优先级；切页横幅（1200ms，基线 7/14，`shortTitle` + `n/N`）次之。
- 覆盖层用 `CLOCK_MONOTONIC` 计时：中键单击 IP 12s / `NO WIFI` 6s /
  AP IP 12s，右键长按进入 AP 显示 SSID+固定密码两行 10s、退出 `AP OFF` 4s、
  进入失败 `AP ERR` 6s；HTTP 错误 6s。
- **IP 覆盖层**：单行 scale 2 整屏居中（基线 13），宽度 >52px 时按
  60ms/像素水平滚动（周期 = 文本宽 + 8px），无“IP”前缀；`NO WIFI` 仍两行小字。
- `sendLedData` 把每行补到 64 列，右侧 12 列恒黑。

## 5. 配置持久化与迁移

### 5.1 `/data/multitimer.conf`（v3）

- 完整 schema、字段规则与示例见 [`V3-CONTRACT.md`](V3-CONTRACT.md) §2 与
  [`STATE-2026-10-22-v3.md`](STATE-2026-10-22-v3.md) §5；`toJson` 字段顺序稳定
  （`version`/`tzOffsetMinutes`/`warnSeconds`/`ntpServer`/`colors`/`display`/
  `targets`/`live`/`defaultPageId`/`pages`），颜色统一大写。
- `ConfigStore::save()` 先写 `.tmp`，`fflush`+`fsync`+`fclose` 后 `rename` 原子替换；
  写临时文件由互斥锁串行化。
- `fromJson`（`POST /api/config` 用）**只接受 v3**：`version` 缺省或 3，
  必须有 `pages`；v2 → 400 `v2 config is not accepted, refresh the page`；
  v1 → 400 `v1 config is not accepted, refresh the page`。
- 严格字段：`tzOffsetMinutes` 必填、`warnSeconds` 0..600、颜色
  `clock/countdown/countup/warning/dim`（`#RRGGBB`，countup 缺省 `#00BFFF`）、
  `targets` ≤12（单项非法丢弃、超限整份拒绝）、`pages` 1..12（未知 type 400）、
  live 页 `rooms` 1..2、`live.url` 仅 `http://`（≤160、无空格）、
  `refreshSec` 15..300、`timeoutMs` 1000..5000。
- 宽容/修正字段：`pages[].id` 非法/重复自动生成；`title` 截到 16 字符并剔除
  控制字符；`shortTitle` 只保留可打印 ASCII ≤8；`label` 转大写、只留 `[A-Z0-9._-]`
  ≤3；`defaultPageId` 非法回退第一页；`display.*` 非法取默认
  （该字段已废弃：保留解析/回写兼容，渲染忽略 `blank`）。
- 请求体 16 KB、规范化配置 ≤12 KB（HttpServer 层 413 `config too large`）。

### 5.2 v1 → v2 → v3 迁移

- 只在 `ConfigStore::load()` 读盘时执行；`POST` 不接受旧 schema。
- v1（`tzOffset` 小时、`points`）先按 v2 规则迁移：`tzOffset×60`、
  `points` → 当前本地日期的一次性目标（时间无效时不迁移、保留原文件，等时间有效后补）；
- 再补 v3 字段与页面（见下）；迁移成功后 `mainLogic` 立即 `save()` 写回 v3（只写一次）。

### 5.3 v2 → v3 迁移（契约 §2）

- 判定：`version == 2`，或无 `version` 但含 `tzOffsetMinutes`/`targets` 且没有 `pages`。
- 保留：`tzOffsetMinutes`、`warnSeconds`、`colors.clock/countdown/warning`、
  `targets`、`ntpServer`。
- 新增：`colors.dim="#808080"`、`colors.countup="#00BFFF"`（缺省补默认）、
  `display.splitBottomWhenIdle="date"`（保留兼容/已废弃）、
  `live={source:"off",url:"",refreshSec:30,timeoutMs:3000}`。
- `pages` 固定补 3 页：`p0 clock`、`p1 clock_countdown`、`p2 countdown`；
  `defaultPageId="p0"`；**不自动创建 live 页**。
- 全新安装（无配置）：默认 4 页，追加 `p3 live`（2 个空槽、source=off）。
- `version >= 4` 读盘失败：保留原文件、RAM 用默认配置（避免覆盖更高版本文件）。

### 5.4 WiFi 凭据优先级与 v2 列表（不变）

1. `/data/multitimer.wifi`（本应用 v2 JSON 列表，最高，≤8 个）
2. `/data/misc/wifi/wpa_supplicant.conf`
3. `/data/setting.ini`
4. `/mnt/usb1/test.cfg` / `/mnt/extsd/test.cfg`

```json
{"version": 2,
 "networks": [{"ssid": "home-2g", "password": "<password>", "enabled": true}]}
```

- 旧单对象 `{"ssid","password"}` 自动兼容为一个启用网络；合法空列表
  `networks: []` 不回退后续来源（删文件才回退）。
- `GET /api/wifi` 只返回 `hasPassword`，**绝不返回密码**；密码不写日志、不回显。
- `POST /api/wifi/list` 整表覆盖：同 SSID 空密码保留原密码，新 SSID 空密码按开放网络；
  重复 SSID / >8 / 类型错误 400；保存成功后约 800ms 断开重连（AP 下先退出 AP）。
- 密码明文保存在设备 `/data`；仓库文档不记录真实密码。

### 5.5 网页资源

运行时读 `src/web/editor_gz.h`（gzip 字节），不是 `web-src/index.html`。改网页后必须：

```powershell
python .\native\MultiTimer\tools\build_web_asset.py
node .\native\MultiTimer\tools\check_web.js
```

再编译原生工程；`editor_gz.h` 是构建产物，不要手改。

## 6. HTTP 接口（v3）

监听 `0.0.0.0:8080`，每连接独立线程，无鉴权、无 TLS、无限流，仅可信局域网。
请求头 2 KB、请求体 16 KB、收发超时 3s、最多 8 个并发连接；80 端口只做 302。

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/`、`/index.html` | 内嵌手机页（`Content-Encoding: gzip`） |
| GET | `/api/config` | 从磁盘重读配置；返回 `{ok,config,device,ui,live}`（`config` 永远 v3；`device.page` 与 `ui` 同义） |
| POST | `/api/config` | v3 schema（或 `{"config":{...}}`）→ 校验 → 保存 → 热应用；v2/v1 400；成功返回 `{ok,ui,page}` |
| POST | `/api/page` | `{"id":"p1"}`/`{"index":1}` 切页（不写配置，返回 `{ok,index,pageCount,page}`）；`{"reset":true}` 清 uiState |
| GET | `/api/live/status` | 数据源健康 + 全部已配置房间快照（≤24） |
| POST | `/api/live/refresh` | 触发异步刷新（`{"page":N}` 可省）；返回 `{ok,queued,page}` |
| POST | `/api/live` | `push` 模式写缓存（body 同 bridge 响应）；返回 `{ok,updated,ts}` |
| POST | `/api/time` | `{"epoch": 1700000000..4102444800}` 立即写系统时钟并持久化 |
| GET | `/api/wifi` | 当前网络 + 网络列表；只返回 `hasPassword` |
| POST | `/api/wifi/list` | 保存完整列表（≤8）并异步重连；AP 下先退出 AP |
| POST | `/api/wifi` | 兼容旧页面单网络接口；空密码不清空原密码 |
| GET | `/api/status` | `{ok,device,state}`；`state` 增加 `text2`/`banner`/`page{}`；`device.page` 为兼容别名 |
| OPTIONS | `*` | CORS 204 |
| GET | `/favicon.ico` | 204 |
| 其它 | — | 404 `{"ok":false,"error":"not found"}` |

- `GET /api/config` 的 `ui`：`{pageId,pageIndex,pageCount,pageTitle,pageType}`；
  `live`：`{source,refreshSec,reachable,lastOkUtc,error}`（运行时只读，POST 不接受）。
- `GET /api/status` 的 `state.page`：`{id,index,title,type,count}`；
  `mode` 为 `clock|countdown|countup|invalid`（页面计时部分；`countup` 自 v3.2 起），
  页面类型看 `page.type`；`text`/`text2`：datetime 页 `text=HH:MM:SS`、
  `text2=YYYY-MM-DD`，无目标时 countdown `text=NO COUNTDOWN`、分屏 `text2=NO COUNTDOWN`；
  `state.overlay=true` 表示 IP/NO WIFI 覆盖层正在显示。
- **网页 v3.3 实际使用的接口**：`GET /api/config`、`POST /api/config`、`POST /api/time`、
  `GET /api/wifi`、`POST /api/wifi/list`、`POST /api/page`；
  直播状态/刷新接口（`/api/live/status`、`/api/live/refresh`）为固件 API，
  网页不调用，供脚本/后续版本排查与联动。
- 错误统一 `{"ok":false,"error":"..."}`；保存失败 500，连接超限 503。
- 80→8080：启动时同时监听 80，用 `getsockname` 取本端 IP 后 302；80 失败只 warning。
- 配网模式只能由**右键长按 3s**进入/退出，不提供 HTTP 开关接口。
- 全部时间/日期字段格式：`HH:MM:SS` / `YYYY-MM-DD HH:MM:SS` / `YYYY-MM-DD`。
- **已知差异（QA P2）**：配置缺失/损坏的极端情况下 `GET /api/config` 曾可能返回
  `pages: []`（已在 QA 后修复：无配置时返回默认 v3 4 页，与运行时兜底一致）；
  `/api/live/status` 实际
  形状为 `{ok,source{kind,url,reachable,lastAttemptUtc,lastOkUtc,error},refreshSec,
  timeoutMs,activePage,rooms[]}`，与契约 §6 的简表字段名有差异（实现更全）。
  以本文/实际响应为准，契约 §6 待更新（v3.0 QA 基线，已不覆盖 v3.3 的 B 站直连/正计时）。

## 7. 网页端（v3）

单文件、零外链、移动优先、深色；自检 `node native/MultiTimer/tools/check_web.js`
**90/90 通过**。卡片顺序：

1. **时钟连接（自动）**：不再有 IP 地址输入/保存列表；只用当前页面 origin
   （`:80`→`:8080`，无端口补 `:8080`）；`file://` 打不开时提示“请通过设备 IP 打开页面”；
   设备信息（IP/SSID/时间/时区/对时/运行时长）。
2. **网络与对时**：当前 WiFi/IP；WiFi 列表编辑器（≤8，增删/启停/排序/编辑/保存并重连）；
   NTP 服务器输入（留空内置）；立即对时。
3. **时区与显示**：常用时区 + 自定义分钟偏移（-720..840）；
   颜色（时钟/倒计时/**正计时**/预警/暗色）+ 预警秒数；旧“分屏下半 blank”设置已移除。
4. **倒计时目标**：≤12，日期控件 + 时间控件（`step=1`，提交 `HH:MM:SS`）；
   本地备注只存 localStorage；上移/下移/删除/移除已过期。
5. **页面**（v3 新增）：页面列表（序号/标题/类型/默认/当前；类型含“日期 + 时间”；↑↓/删除/新增/编辑；
   “设为此页”调 `/api/page`，“设为默认页”改 `defaultPageId`）；选中页编辑
   （标题/`shortTitle`/类型/启用）；live 页额外有本页默认指标 + **2 个直播槽位**
   （平台/房间号/label/启用/指标覆盖“跟随页默认”）；指标随平台联动，
   房间号支持粘贴对应平台链接并始终按字符串保存；**直播数据源（全局）**子卡：
   `off`/`bridge`/`hybrid`/`push` + URL + `refreshSec` + `timeoutMs`。
6. **52×16 模拟器**：按页预览 5 类页面（分屏 8+8、直播上下两行、**正计时帧**、**datetime 帧**）、横幅；
   跟随真实时间或手动日期+时间（带秒）；1x/10x/60x/300x/3600x 播放；
   直播页可选“直播预览数据：无/示例数据”（不读取设备实时数据）。
7. **保存**：`POST /api/config`（v3）；成功后回读配置并静默对时一次。
8. **状态日志**：错误完整展示、不自动滚动。

> 网页**不含**“直播监控”卡：`GET /api/live/status`（房间状态/年龄/错误）与
> `POST /api/live/refresh` 只在固件 API 提供，供 curl/脚本/后续版本使用；
> 网页 v3.3 的验证边界以此为准。直播数据源下拉/提示已在最终版更新为
> “B 站设备直连 + 抖音/快手中继”，与固件一致。

行为要点：

- 保存前校验与固件一致（版本、`tzOffsetMinutes`、颜色、`warnSeconds`、`ntpServer`、
  目标、页面/房间/指标、`live.source/url/refreshSec/timeoutMs`）；
- WiFi 密码字段不回显、不写 localStorage、不写状态日志；同 SSID 留空保留原密码，
  新 SSID 留空按开放网络；保存成功后延迟约 2.5s 回读；
- 设备返回 v2 旧固件时，页面编辑区禁用并提示先升级/刷入 v3；
- 所有不可信文本（title/label/room/SSID/错误）只进 `textContent`，不拼 `innerHTML`；
- 页面内部时间换算只用 `tzOffsetMinutes`（分钟），目标省略秒时补 `:00`。

## 8. 线程与安全注意

- **锁顺序**：`PageHost.mMutex` → `LiveService.mMutex` 不嵌套。
  `PageHost::tick()` 在 PageHost 锁内取标量/页配置，**解锁后**再取 LiveService 快照，
  渲染在无锁区；持有 LiveService 锁的地方不回调 PageHost。
- **PageHost**：配置/当前页/覆盖层/状态由 `std::mutex` 保护；按键/HTTP 线程只改状态
  + 置脏，不渲染、不发 SPI、不做网络/存储 I/O；`getStatus()` 拷贝快照。
- **LiveService**：单 detach 后台线程（`start()` 幂等，退出 `shutdown()`）；
  网络 I/O 不持锁（先取配置快照、解锁后请求、回写时再取锁）；同一时刻只允许
  1 个在途请求；切页/按键置 urgent，当前请求结束后立刻再发；不画屏、不写 flash、
  不回调 PageHost。
- **uiState 落盘**：UI 线程 `tick()` 内做节流判定，`StoragePreferences::putString`
  在锁外调用；只有 1 页时不写；失败保持 pending，下一次 ≥120s 后重试。
- **TimeKeeper**：互斥锁保护 `mLastSyncUtc`/`mLastPersistUtc`；`tick()` 在 UI 线程
  每秒调用、内部 300s 节流；落盘放在页面绘制之后。
- **TlsClient**：函数指针表用 `std::call_once` + mutex 初始化一次，之后只读；
  每次 `get()` 独立 SSL_CTX/SSL/socket，不共享可变全局状态；
  不在 UI 线程同步调用（B 站取数在 LiveService 后台线程）。
- **HttpClient / LiveService**：只发到 `http://`，不打印完整 URL（可能含 token）；
  bridge 响应/推送体原始上限 16 KB，B 站原始响应上限 64 KB。
- **AP 与 STA 互斥**（v2.1 语义不变）：进入 AP 会释放 STA/DHCP，`WifiBootstrap`
  在 AP 期间暂停；`ConfigMode::exit()` 只能 UI 线程调用，HTTP/后台线程用
  `requestExit()`；`touch()` 由网页 API 活动续期（GET 静态页不续期）。
- **HTTP 并发**：每连接线程 + detach accept；最多 8 个活跃连接，超限 503；
  3s 收发超时；`POST /api/config` 的 `applyCallback` 内不做阻塞 I/O。
- **不要重新引入 `std::atomic<long long>`**：Z21 32 位 ARM 工具链没有自动链接
  `libatomic`（曾导致 `undefined reference to __atomic_load_8`）；用 32 位
  `atomic<unsigned long>` 存秒级时间戳。
- **`dlopen` OpenSSL**：工具链 sysroot 没有 OpenSSL 头文件与 libssl，`-ldl` 已在
  链接参数中；不要修改 `.cproject` 链接参数。v3.3 镜像把 OpenSSL 1.1.1w 放到
  `/res/lib`，`TlsClient` 优先加载它；库/符号缺失时回退系统 1.1.0i（无 TLS client），
  房间显示 `--`，不影响其它功能。
- **安全**：HTTP 8080/80、AP 配网页均无鉴权/无 TLS，只应放可信局域网；
  WiFi 密码、配置为明文文件；直播 bridge 只接受局域网明文 HTTP、无 token；
  不保存任何用户 cookie/token（bridge 的匿名 `ttwid` 只在 PC 内存、2h 过期）。

## 9. 构建命令与验证记录

### 9.1 GUI

1. FlyThings IDE 2.x 导入 `native/MultiTimer`；
2. 右键 → 更新依赖（本机已缓存则无需联网）；
3. `Ctrl + Alt + Z` 或工具栏绿色三角；
4. 产物 `Release/libzkgui.so`。

### 9.2 Headless（推荐）

```powershell
# v3.3 构建（独立 workspace/日志；最终版 2026-10-22 21:49:02 已构建）
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1 `
  -Workspace build\v3g-workspace -LogPath build\v3g-build.log
```

参数：`-ProjectDir`（默认 `native/MultiTimer`）、`-Workspace`（默认
`build/ide-workspace`）、`-Configuration`（默认 `Release`）、`-LogPath`
（默认 `build/headless-build.log`）。脚本会重建 workspace、复制工程并执行：

```text
ide.exe -nosplash -data <Workspace> -application org.eclipse.cdt.managedbuilder.core.headlessbuild
        -import <Workspace>\MultiTimer -cleanBuild MultiTimer/Release
```

判定：**不因 clean 的失败退出码直接失败**；只有生成非空
`<Workspace>/MultiTimer/Release/libzkgui.so` 才算成功。并行构建必须用独立
workspace + 日志（`build/v3g-workspace` 等为可删除产物，不提交）。

### 9.3 测试

```powershell
# 主机单测（211 用例；真实编译 src 下纯 C++ 模块）
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1

# 网页逻辑自检（90 项）
node .\native\MultiTimer\tools\check_web.js

# Bridge 离线单测（40 用例，见 docs/VERIFY-bridge.md）
cd tools\live-bridge; python -m unittest -v test_bridge; cd ..\..
```

### 9.4 本次验证结果（2026-10-22 22:02:00，v3.3）

| 项目 | 结果 |
|---|---|
| 交叉编译（v3.3 最终） | **0 errors / 0 warnings**（`build/v3g-build.log`，21:49:02） |
| 最终源码产物 | `build/v3g-workspace/MultiTimer/Release/libzkgui.so`，**722,532 字节**，SHA256 `1A4959919B126AC35CE673BF14799A83CFE767063CA61A17896E882E07B8086E` |
| 历史产物 | v3.0（718,436 B / `A4B1D7E8...`）、QA 基线（`E92CAFD0...`）、更早 workspace 仅作历史 |
| 主机单测 | **211/211**（TimeMath 24 + TimerEngine 19 + ConfigStore 54 + mini_json 10 + WifiStore 19 + PageRenderer 44 + LiveFormat 24 + BilibiliClient 17；新增 datetime/NO COUNTDOWN 渲染用例） |
| 网页自检 | **90/90**（含“分屏无目标始终 NO COUNTDOWN/旧 blank 被忽略”、正计时颜色、自动地址、datetime 选项） |
| Bridge 单测 | **40/40**（`Ran 40 tests ... OK`） |
| QA 报告 | 仅 v3.0 基线：[`QA-v3-initial.md`](QA-v3-initial.md)、[`QA-v3-final.md`](QA-v3-final.md)；**v3.3 暂无独立 QA** |
| 镜像 | `dist/update.img` **1,942,076 B** / `8B2BCF20A71202610FAC6CA9C2A3D339B8D457E2C48D358B7C791933114C16B4`；`res.tmp` 1,941,504 B / `92E2209A...9160`（21:49:28 生成，8 MiB 的 **23.15%**）；镜像 227 文件（含 2 个 OpenSSL 库） |
| 真机 v3.3 | **已固化并验证通过**：回读 `/res/lib/libzkgui.so` 722,532 B / `1A495991...`、`/tmp` 空、`/data` 保留；NO COUNTDOWN（p1 text2 / p2 text）、datetime 页（text=22:01:27 / text2=2026-10-22）、B 站直连、网页自动地址证据见 [`STATE-2026-10-22-v3.md`](STATE-2026-10-22-v3.md) §10.2 |

构建日志里可能出现：`Duplicate identifier gnu.cpp.compiler.option.dialect.std`
（IDE manifest 已知告警）、`cann't find any dirctory`（收尾无关输出）、
GCC 7.1 ABI note（工具链 libstdc++ 提示）——都不是本项目错误。

### 9.5 并行开发约定

同一 workspace 或同一日志路径不能并发构建；多人/多智能体时每人独立
`-Workspace build\<agent>-workspace -LogPath build\<agent>-build.log`；
`build/ide-workspace*`、`build/v212-workspace`、`build/v3g-workspace` 等均可删除。

### 9.6 测试边界

- 主机单测覆盖不依赖 FlyThings SDK 的纯 C++ 模块：TimeMath、TimerEngine、
  ConfigStore（v3/迁移/严格校验）、mini_json、WifiStore、PageRenderer（像素级
  4 页面 + 横幅/覆盖层）、LiveFormat、BilibiliParse/BilibiliClient（解析/URL/
  错误路径，TLS 不打真实网络）、HttpClient（URL/响应解析）。
- 无主机单测、依赖真机/网页自检的：`TimeKeeper`、`PageHost`/`TimerPage`（SPI）、
  `LiveService`（线程/调度，用集成审查+真机）、`HttpServer`（socket）、
  `TlsClient`（dlopen/真机）、`ConfigMode`、`WifiBootstrap`/`SntpClient`。
- 当前没有 CI；每次改源码后必须重跑 `tests\run_tests.ps1` 与
  `node native\MultiTimer\tools\check_web.js`，并在真机复核关键路径。

## 10. 已知限制 / 技术债（v3 增补）

v2.1.2 的历史限制（无 RTC、无 tzdata/不自动 DST、没有每日循环、最多 12 目标、
HTTP 无鉴权、复位键清 `/data`、2038、`/tmp` 易失、SNTP 依赖 UDP 123、
镜像 8 MiB、防砖依赖 `sys.zkapp.state`、内嵌网页是构建产物等，见
[`STATE-2026-10-22-v21.md`](STATE-2026-10-22-v21.md) §7）继续适用。v3 新增：

1. **直播平台接口变化/风控**：B 站 `getH5InfoByRoom` 是网页内部接口
   （`getInfoByRoom` 已被 -352）；抖音匿名路径会被 IP 级限流（`200`+0 字节）；
   快手需真实浏览器/JS 签名。bridge 只能缓解，不能消除。
2. **B 站直连依赖镜像自带的 OpenSSL 1.1.1w**：镜像 `/res/lib` 缺失/被替换时
   会回退系统 1.1.0i（无 TLS client，直连不可用，只能走 bridge）。升级镜像时
   不要漏掉 `res-extra` 的 2 个库；OpenSSL 1.1.1w 已 EOL（2023-09），需关注安全公告。
   B 站直连失败且 `live.url` 非空时回退 bridge（60 分钟退避）。
3. **bridge 明文 + 无鉴权**：只适合可信局域网，不要公网暴露；设备只接受 `http://`；
   v3.3 下 bridge 主要承担抖音/快手（B 站直连失败时兜底）。
4. **抖音限流**：匿名 `web_rid` 路径不稳；建议抖音房间 ≤2、`refreshSec` ≥30，
   或使用 19 位 `room_id`（`reflow` 端点）。
5. **快手必须 Playwright**（PC/NAS 约 200–400 MB 内存）；未安装返回
   `kuaishou_requires_browser`，屏幕显示 `--`。
6. **内存/线程**：新增 LiveService 1 线程；TLS 上下文只在 B 站直连路径创建，
   失败后 60 分钟退避；镜像新增 2 个 OpenSSL 库（payload 23.15%），设备约 35 MB
   内存，仍需真机长期观察。
7. **当前页持久化节流**：掉电最多丢最近 2 分钟内的页位置；只有 1 页时不写。
8. **页面标题不上屏**：`title` 只用于网页/API；横幅用 `shortTitle`（ASCII ≤8），
   缺失时回退类型短名（`clock_countdown` → `CLOCK+CD`）。
9. **v3 配置对 v2 固件不可读**：回滚 v2 固件时 v2 会因 `version!=2` 按默认配置运行；
   回滚前备份 `/data/multitimer.conf`。
10. **v3.3 真机验证已完成（2026-10-22）**：NO COUNTDOWN/datetime 页/正计时/B 站直连/网页自动地址均通过；
    仍待用户确认项（**物理中键单击 IP / 右键长按 SoftAP 的 LED 观感**、8 WiFi 切换、
    24h+ 稳定性、B 站直连长期稳定性等）
    见 [`STATE-2026-10-22-v3.md`](STATE-2026-10-22-v3.md) §10.2。

QA 报告（v3.0 基线；[`QA-v3-initial.md`](QA-v3-initial.md) §7）的 P2 项也属于交接必须知道的差异；v3.3 新增的正计时/NO COUNTDOWN/datetime/按键/IP/自动地址/B 站直连尚无独立 QA：

11. **`GET /api/config` 在配置缺失/损坏的极端情况下返回 `pages: []`**（P2-1）：
    **已在 QA 后修复**（`loadConfigOrDefault` → `ConfigStore::defaultConfig()`），
    无配置/损坏时返回默认 v3（4 页），已包含在最终构建。
12. **B 站直连超时曾固定 4000ms/端点**（P2-3）：**已在 QA 后修复**——
    `BilibiliClient::fetchRoom` 现在传入 `live.timeoutMs`；直连失败后 60 分钟退避，
    `live.url` 非空时同轮回退 bridge。
13. **TlsClient 每次请求新建 SSL_CTX + 重载 CA**（P2-4）：仅 B 站直连路径使用；
    失败时 60 分钟退避，常规成功请求每次重载 CA，属可接受的已知开销。
14. **固件与网页模拟器对“空直播槽”占位不同**（P2-6）：固件启用但空房号=留黑，
    整页无已配置房间才 `NO LIVE`；网页模拟器对“启用+空房号”显示 `label + --`。
    以固件为准。
15. **live 页稳态 `/api/status` 的 `nowUtc` 可能滞后**（P2-9）：诊断精度问题。
16. **旋钮 120ms 消抖**（P2-7）：快速连转（<120ms/格）可能丢格，真机验证手感。
17. **设计文档残留旧版式**（P2-10）：`DESIGN-pages-v3.md`/`DESIGN-web-v3.md`
    未更新（“4 房间/2×2”、无 datetime 等）；以 [`V3-CONTRACT.md`](V3-CONTRACT.md)
    §2/§3 与实现（5 类页面、2 行/2 房间）为准，设计文档待后续清理。
18. **契约残留旧描述**（P2-11）：`V3-CONTRACT.md` §1/§5 仍有“B 站直连不可用/
    hybrid+url 三平台都走 bridge”等旧句，与 v3.3 实现（B 站直连可用、bridge 负责
    抖音/快手）冲突；以本文与源码为准，契约 §1/§5 待更新。
19. **60 分钟直连退避对所有直连失败类型生效**（P2-12）：v3.3 直连已可用，
    一次瞬时失败会导致 60 分钟不再直连（建议按错误类型退避）；
    网页直播数据源文案已在最终版更新为“B 站设备直连 + 抖音/快手中继”。

## 11. 验证清单

### 已完成（主机/静态）

- [x] v3.3 最终源码交叉编译 **0 errors / 0 warnings**（21:49:02，`build/v3g-build.log`）；
- [x] 主机单测 **211/211**（含正计时/NO COUNTDOWN/datetime 渲染用例）；
- [x] 网页自检 **90/90**（页面/直播编辑、正计时/NO COUNTDOWN/datetime/自动地址、模拟器、v2 回归、XSS、格式）；
- [x] bridge 离线单测 **40/40** + 本机实测（B 站/抖音混排、错误路径、4KB 上限）；
- [x] 只读 QA 报告为 v3.0 基线（P0=0/P1=0/P2=10，P2-1/P2-3 修复复核）；
      v3.3 增量由 Lead 实现并真机验证，尚无独立 QA；
- [x] 网页自检 **90/90**（页面/直播编辑、模拟器、v2 回归、XSS、格式）；
- [x] bridge 离线单测 **40/40** + 本机实测（B 站/抖音混排、错误路径、4KB 上限）；
- [x] v2→v3 迁移、v1→v3 迁移、`POST` 拒收 v2/v1、`pages`/`rooms` 上限与容错由
      ConfigStore 用例覆盖（含 `colors.countup` 缺省补默认）；
- [x] 页面渲染像素级用例（clock/clock_countdown 8+8/countdown/**正计时/NO COUNTDOWN**/
      **datetime**/live 两行/横幅/覆盖层）；
- [x] 直播格式化（`w`/`y`/`LIVE`/`OFF`/`--`）与 B 站解析/URL/错误映射用例；
- [x] 内嵌网页重新生成（`editor_gz.h`，21:49:01；与最终 `web-src/index.html` 一致）；
- [x] 生成 v3.3 镜像并固化 `/res`（`update.img` 1,942,076 B / `8B2BCF20...`，21:49:28；
      回读 `/res/lib/libzkgui.so` 722,532 B / `1A495991...`；`/tmp` 空、`/data` 保留）；
- [x] 真机 NO COUNTDOWN（p1 text2 / p2 text）、datetime 页
      （text=22:01:27 / text2=2026-10-22）、B 站设备直连（bridge 停止仍 `state=ok`）、
      页面/迁移、网页自动地址（详见 [`STATE-2026-10-22-v3.md`](STATE-2026-10-22-v3.md) §10.2）。

### 待用户确认 / 长稳（不要当成已验证）

- [ ] **物理中键单击显示 IP、右键长按 3s 进入/退出 SoftAP 的 LED 观感**
      （IP 滚动速度、AP SSID+密码两行、AP OFF/AP ERR）与按键手感；
- [ ] 8 个 WiFi 网络逐个失败/成功切换与重连耗时；
- [ ] 自定义 NTP 真机校时/失败回退、每分钟 NTP 长日志；
- [ ] 预警色/远目标 `>=100h` 小号字/`--:--:--` 目视确认；
- [ ] 网页真机保存热生效、390px 布局；
- [ ] `uiState` 重启恢复（最后停留页 > defaultPageId > 第一页）；
- [ ] 24h+ 稳定性/内存/线程/HTTP 并发；复位键恢复官方固件演练；
- [ ] B 站直连长期稳定性（接口/证书/时间/弱网）。

## 12. 仓库整理记录（v3 增补）

- v3 新增：`src/pages/PageTypes.h`、`PageRenderer.*`、`PageHost.*`、
  `src/live/**`、`src/net/HttpClient.*`、`TlsClient.*`、
  `BilibiliClient.*`、`BilibiliParse.*`、`tests/test_page_renderer.cpp`、
  `tests/test_live_format.cpp`、`tests/test_bilibili_client.cpp`、
  `tools/live-bridge/**`、`docs/V3-CONTRACT.md`、`docs/DESIGN-pages-v3.md`、
  `docs/DESIGN-web-v3.md`、`docs/RESEARCH-live-apis.md`、`docs/LIVE-BRIDGE.md`、
  `docs/VERIFY-bridge.md`、`docs/STATE-2026-10-22-v3.md`、
  `native/MultiTimer/res-extra/**`（OpenSSL 1.1.1w）、
  `native/MultiTimer/third_party/openssl-armhf/**`；
- 修改：`TimerTypes.h`、`ConfigStore.*`、`HttpServer.*`、`TimerPage.*`、
  `mainLogic.cc`、`web-src/index.html`、`tools/check_web.js`、`src/web/editor_gz.h`；
- 保留不动：v2/v1 历史快照（`STATE-2026-10-22.md`、`-v2.md`、`-v21.md`）、
  `docs/FLASH-RUNBOOK.md` 的 v2/v2.1 历史记录、官方 SDK 资料
  （`doc.md`/`doc.html`/`repo_readme_cn.md`/`demo_*`/`tree.json`）、
  `legacy/awtrix-ng/`；
- 本轮 v3.3 尚未 `git commit`（由 Lead 提交）；`dist/` 已更新为 v3.3（2026-10-22 21:49:28）。

## 13. 文档索引

- 使用/编译/部署/排障：[`native/MultiTimer/README.md`](../native/MultiTimer/README.md)
- 当前快照：[`STATE-2026-10-22-v3.md`](STATE-2026-10-22-v3.md)
- 契约：[`V3-CONTRACT.md`](V3-CONTRACT.md)
- QA 报告（v3.0 基线）：[`QA-v3-initial.md`](QA-v3-initial.md)、
  [`QA-v3-final.md`](QA-v3-final.md)
- 页面设计：[`DESIGN-pages-v3.md`](DESIGN-pages-v3.md)
- 网页设计：[`DESIGN-web-v3.md`](DESIGN-web-v3.md)
- 直播调研：[`RESEARCH-live-apis.md`](RESEARCH-live-apis.md)
- Bridge：[`LIVE-BRIDGE.md`](LIVE-BRIDGE.md)、[`VERIFY-bridge.md`](VERIFY-bridge.md)
- 固化：[`FLASH-RUNBOOK.md`](FLASH-RUNBOOK.md)
- 发布包：[`dist/README.md`](../dist/README.md)
- 主机单测：[`tests/README.md`](../tests/README.md)`n- OpenSSL 1.1.1w：[`native/MultiTimer/third_party/openssl-armhf/README.md`](../native/MultiTimer/third_party/openssl-armhf/README.md)
- v2.1.2 历史快照：[`STATE-2026-10-22-v21.md`](STATE-2026-10-22-v21.md)











