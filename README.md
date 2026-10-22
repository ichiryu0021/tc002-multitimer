# Ulanzi TC002 MultiTimer v3.3 — 可翻页页面系统 + 正计时/日期页 + 直播监控

> **公开仓库说明**：本仓库开源固件应用源码、构建脚本、测试、预编译 `libzkgui.so`，以及**最小 /res 刷机镜像 `dist-minimal/update.img`**（只含本项目文件、公共 CA、OpenSSL；不含官方 stock /res 的厂商素材）。
> 含官方 stock `/res` 的完整镜像不随仓库分发；如需完整镜像，请在本地用官方固件抽取 stock 基线后运行 `tools/make-image.ps1`，见 `docs/FLASH-RUNBOOK.md`。许可证 GPL-3.0；第三方组件见 `THIRD_PARTY_NOTICES.md`。

> 旧的 AWTRIX NG / Berry / Python 方案未包含在公开仓库中；本仓库只维护基于 Ulanzi 官方 SDK 的 FlyThings 原生方案。

## 公开 Release：最小可刷镜像

- 文件：`dist-minimal/update.img`（1,491,516 B，SHA256 `B079F8DA0A3443D4B8E21D0B5D2BA51284F1537400630260ECF8CEA9E012F4F2`）
- 内容只有 6 个文件：`lib/libzkgui.so`（本项目 GPL 应用）、`lib/libssl.so.1.1`、`lib/libcrypto.so.1.1`、`ui/main.ftu`（官方 GPL Demo 资源）、`ui/cacert.pem`（公共 CA，MPL-2.0）、`etc/EasyUI.cfg`（本项目配置）
- **不含** Ulanzi stock `/res` 的字体、图标、音频、BlueZ、官方 Web/测试 UI 等素材；因此可以随 GPL 源码一起分发。
- 设备系统 `/lib` 仍需官方固件提供的 `libeasyui` / `libzkhardware` / `libzknet` 等运行库；本镜像只替换 `/res`。
- 构建：`tools/make-minimal-image.ps1`；校验信息见 `dist-minimal/build-info-minimal.txt`。
- 刷机仍用官方 ADB 流程（`tools/flash-update.ps1 -Confirm`）；**刷前自行确认能恢复官方固件**。

本项目是 Ulanzi TC002（52×16 RGB 像素时钟，Z21 平台）的**原生 C++ 固件应用**。
v2 提供“正常时钟 + 带日期多目标倒计时”，v2.1 补齐多 WiFi / SoftAP / 每分钟 NTP /
多时钟地址 / 80→8080 跳转；**v3** 在此基础上加入：

- **可翻页页面系统**：网页编辑页面列表，旋钮 CW/CCW 翻页（跳过停用页、1.2s 横幅）；
  五类页面：`clock`、`clock_countdown`（上下 8+8 分屏，时钟与倒计时同秒）、
  `countdown`、`live`（**上下两行，一页 2 个直播间**，可多页）、
  `datetime`（日期 + 时间：时间大字 + 日期小字）；
- **直播监控**：B 站走设备 **HTTPS 直连**（镜像 `/res/lib` 携带 OpenSSL 1.1.1w，
  系统 1.1.0i 不可用），抖音/快手经局域网 **bridge**（PC/NAS 上的 `tools/live-bridge`，
  明文 HTTP）；`live.source` = `off`/`bridge`/`hybrid`/`push`；`hybrid` 下 B 站直连、
  B 站失败回退 bridge；bridge 负责抖音/快手；
  指标 `online/likes/fans/status`；平台/房间/指标可在网页调整；
- **正计时**：目标到达后从最近的过期目标累计（`HH:MM:SS`），正计时颜色
  `colors.countup` 默认 `#00BFFF`；完全没有有效目标时显示 `NO COUNTDOWN`（dim）；
- **按键与 IP**：中键**单击**显示全屏 IP（无前缀、超宽水平滚动，12s）；**右键长按 3s**
  进出 SoftAP；旋钮 CW/CCW 翻页、旋钮按键刷新直播；
- **网页地址自动**：不再保存 IP 地址列表，只用当前打开页面的 origin（自动补 `:8080`）；
  设备只保存 WiFi、IP 由 DHCP 获取；
- **当前页持久化**：重启恢复上次停留页（节流写 flash，不频繁擦写）；
- v2.1.2 的**多 WiFi、SoftAP 配网、每分钟 NTP、自定义 NTP、80→8080 跳转**保留。

设备独立运行：没有未来目标时按时钟显示，有目标时严格按系统时钟倒计时到最近目标；
配置、时区、颜色、WiFi、页面与直播参数都存在设备 `/data`，不需要电脑或云端服务常开
（直播数据源 `bridge`/`hybrid` 模式除外，需要局域网内一台 bridge 主机）。

> **版本状态（2026-10-22 22:02:00）**：v3.3 已完成 **真机固化与验证**。
> 主机单测 **211/211**、网页自检 **90/90**、交叉编译 **0 errors / 0 warnings**
> （最终源码 `build/v3g-workspace/.../libzkgui.so`，722,532 字节 /
> `1A495991...`）。
> 本地开发记录：`dist/update.img`（21:49:28 生成，1,942,076 字节 / `8B2BCF20...`）已通过官方 ADB
> 流程固化到设备 `/res`：`/tmp` 为空、`/data` 配置保留、回读
> `/res/lib/libzkgui.so` = 722,532 字节 / `1A495991...`。
> 真机已验证：**NO COUNTDOWN**（无目标 p1 `text2` / p2 `text`）、**datetime 页**
> （`text=22:01:27`、`text2=2026-10-22`）、**B 站设备直连可用**
> （bridge 停止后示例房间仍 `state=ok`）、网页自动地址加载、页面/迁移/配置保留；
> 仍待用户确认项（物理中键单击 IP/右键长按 SoftAP 的 LED 观感、8 WiFi 切换、
> 24h+ 稳定性等）见
> [`docs/STATE-2026-10-22-v3.md`](docs/STATE-2026-10-22-v3.md) §10.2。
> 目标/直播配置由用户维护（验证用临时目标已删除、用户原有配置已恢复），
> 具体以设备 `GET /api/config` 为准，本文不对具体目标/房间作断言。

## 快速入口

- **使用 / 编译 / 部署 / 固化 / 故障排查**：
  [`native/MultiTimer/README.md`](native/MultiTimer/README.md)
- **v3 实现契约（冻结）**：[`docs/V3-CONTRACT.md`](docs/V3-CONTRACT.md)
- **v3 快照（当前：v3.3）**：[`docs/STATE-2026-10-22-v3.md`](docs/STATE-2026-10-22-v3.md)
- **v3.0 QA 报告（历史基线）**：[`docs/QA-v3-initial.md`](docs/QA-v3-initial.md)、
  [`docs/QA-v3-final.md`](docs/QA-v3-final.md)
  （均为 v3.0 基线；其中“B 站直连不可用”已被 v3.3 取代，v3.3 暂无独立 QA 报告）
- **页面/渲染设计**：[`docs/DESIGN-pages-v3.md`](docs/DESIGN-pages-v3.md)
  （注：设计稿中“一页 4 个直播间/2×2”是旧版式，以实现与 V3-CONTRACT 为准：
  一页 2 个、上下两行）
- **网页配置器设计**：[`docs/DESIGN-web-v3.md`](docs/DESIGN-web-v3.md)
  （同上，live 槽位以实现为准）
- **直播接口调研**：[`docs/RESEARCH-live-apis.md`](docs/RESEARCH-live-apis.md)
- **直播 bridge 协议/部署**：[`docs/LIVE-BRIDGE.md`](docs/LIVE-BRIDGE.md)；
  实测记录 [`docs/VERIFY-bridge.md`](docs/VERIFY-bridge.md)
- **/res 固化 Runbook**：[`docs/FLASH-RUNBOOK.md`](docs/FLASH-RUNBOOK.md)
- **v2.1.2 冻结快照（历史基线）**：[`docs/STATE-2026-10-22-v21.md`](docs/STATE-2026-10-22-v21.md)
- **原生工程**：[`native/MultiTimer/`](native/MultiTimer/)（FlyThings + Z21，C++）
- **构建脚本**：[`tools/build.ps1`](tools/build.ps1)（Windows headless 构建）
- **镜像与烧写脚本**：[`tools/make-image.ps1`](tools/make-image.ps1)、
  [`tools/flash-update.ps1`](tools/flash-update.ps1)
- **发布包 / 组件**：[`dist/`](dist/)（只含 `libzkgui.so`、`EasyUI.cfg`、`ui/main.ftu`；含官方 stock 的 `update.img` 需本地生成）
- **最小可刷镜像**：[`dist-minimal/update.img`](dist-minimal/update.img)（无官方 stock 素材，GPL/公共 CA/OpenSSL）
- **开发交接**：[`docs/HANDOFF-native.md`](docs/HANDOFF-native.md)
- **主机单测**：[`tests/`](tests/)（211/211）
- **直播中继**：[`tools/live-bridge/`](tools/live-bridge/)（Python）
- **废弃方案存档**：[`legacy/awtrix-ng/`](legacy/awtrix-ng/)

## 功能

### v3：页面系统

- **页面列表可配置**：1..12 页，数组顺序 = 旋钮顺序；每页可设类型、标题、短标题、
  启用开关；可设默认页（`defaultPageId`）。配置可在网页可视化编辑。
- **五类页面**：

  | 类型 | 屏幕内容 |
  |---|---|
  | `clock` | 本地 `HH:MM:SS`，大字号（scale 2） |
  | `clock_countdown` | 上半 0..7px 时钟、下半 8..15px 倒计时（两半均 scale 1、同一秒）；目标到达后下半显示**正计时**；完全没有有效目标时显示 `NO COUNTDOWN`（dim） |
  | `countdown` | 倒计时 `<100h` 大字号、`>=100h` 用 `DdHH:MM:SS` 小字号；目标到达后显示**正计时**（不再回时钟）；完全没有有效目标时显示 `NO COUNTDOWN`（dim） |
  | `datetime` | 时间 `HH:MM:SS` scale 2（baseline 10，字距 2→0 自适应）+ 日期 `YYYY-MM-DD` scale 1（baseline 15，dim）；时间无效 `--:--:--` |
  | `live` | **上下两行，每行一个直播间（一页 2 个）**：label（平台字母+房间尾号）+ 指标值 |

- **旋钮翻页**：CW = 下一页、CCW = 上一页；跳过停用页；默认循环；
  切页显示 1.2 秒横幅（`shortTitle` + `n/N`）。
- **旋钮按键**：当前页是直播页时立即刷新直播数据。
- **中键单击**：显示 IP/NO WIFI（全屏 scale 2、无“IP”前缀、超宽水平滚动，12s）。
- **右键长按 3s**：进入/退出 SoftAP 配网（LED 显示 SSID+固定密码两行）。
- **正计时/无目标**：目标过期后显示正计时（`colors.countup`）；没有有效目标显示 `NO COUNTDOWN`（dim）。
- **datetime 页**：新增页面类型，时间大字在上半屏、日期小字在底部（日期用 dim）。
- **当前页持久化**：`uiState` 存 `StoragePreferences`（稳定 30 秒 + 最小间隔 120 秒才落盘）；
  重启恢复优先级：最后停留页 > `defaultPageId` > 第一页。
- 左键不分配。

### v3：直播监控

- **数据源**（`live.source`）：

  | 模式 | 行为 |
  |---|---|
  | `off` | 默认；不产生任何网络请求，直播页显示 `--` / `NO LIVE` |
  | `bridge` | 设备每 `refreshSec` 向局域网 bridge 发 1 个明文 HTTP GET，按当前页批量取 2 个房间（三平台都由 bridge 提供） |
  | `hybrid` | **B 站设备 HTTPS 直连**（镜像携带 OpenSSL 1.1.1w）；抖音/快手走 bridge；B 站直连失败且配了地址时回退 bridge |
  | `push` | 设备不轮询；外部程序 `POST /api/live` 推送 |

- **平台/指标**：B 站设备直连，抖音/快手走 bridge。
  - B 站：`online`（**人气值**，非并发人数）、`likes`、`fans`、`status`、`title`；
  - 抖音：`online`、`likes`、`fans`（19 位 `room_id` 路径）、`status`；
  - 快手：`online`、`likes`、`status`（bridge 需 Playwright）。
- **B 站设备直连（v3.3 已可用）**：镜像 `/res/lib/` 携带 OpenSSL 1.1.1w
  （Debian 11 armhf），`TlsClient` 优先加载；系统 1.1.0i 的 TLS client 被裁掉
  （`SSL_connect` 141640BF），只作回退。真机停止 bridge 后，B 站示例房间仍
  `state=ok`。说明见
  [`native/MultiTimer/third_party/openssl-armhf/README.md`](native/MultiTimer/third_party/openssl-armhf/README.md)。
- **屏幕**：每页 2 个槽位（上行/下行）；label 平台色（B `#FF6699`、D 白、K `#FF9900`）、
  value ≤6 字符（`1234`/`1.2w`/`9.9y`/`--`/`LIVE`/`OFF`）；数值 `0` 是有效值；
  失败/未配置 `--`；整页无配置房间显示 `NO LIVE`。
- **刷新**：`refreshSec` 默认 30 秒（15..300）；切页/旋钮按键/保存配置/WiFi 恢复立即刷新；
  失败保留旧值并按陈旧阈值变暗，不阻塞 UI。
- **网页**：「页面」卡片编辑页面列表与直播 2 槽位、全局直播数据源（`off`/`bridge`/
  `hybrid`/`push` + URL + 刷新周期）；模拟器可预览分屏/直播两行/**正计时帧**；
  颜色卡含**正计时颜色**；地址自动（当前 origin，自动补 `:8080`，不再保存 IP 列表）；
  v3.0 起网页不含直播监控卡，房间状态用 `GET /api/live/status` 查询（API/脚本）。

### 保留的 v2 / v2.1 功能

- **正常时钟**：无未来目标时显示本地 `HH:MM:SS`；联网 SNTP 校时，成功后断网继续走时。
- **带日期多目标倒计时**：`YYYY-MM-DD` + `HH:MM:SS`，最多 12 个；按时间升序、
  同刻去重、过期跳过；到点 `00:00:00` 后切下一个（v2 行为；v3 起由页面系统接管，
  无目标显示 `NO COUNTDOWN`）；不响铃/不闪烁。
- **严格绑定时钟**：每帧（250ms tick）只取一次系统时钟；校时/对时/改时区立即变化。
- **时区分钟偏移**：`tzOffsetMinutes` -720..840，支持 `+05:30`/`+05:45`/`+12:45`；
  不依赖 tzdata、不自动 DST。
- **预警**：剩余 ≤ `warnSeconds`（0..600）常亮红色。
- **掉电保存**：配置写 `/data/multitimer.conf`（原子写）；系统时钟每 300 秒写
  `StoragePreferences["clockState"]`；无 RTC 时重启恢复到最后保存时刻。
- **多 WiFi 列表**：最多 8 个网络，系统自动连接失败后按列表顺序尝试；网页可增删/
  启停/排序/编辑；`GET /api/wifi` 绝不返回密码。
- **SoftAP 配网**：**右键长按约 3 秒**进入/退出；SSID `TC002-Setup`、密码固定
  `12345678`（固件固定值，不是用户 WiFi 密码）；AP IP 实测 `192.168.100.1`；
  15 分钟无 API 活动自动退出。
- **每分钟 NTP / 自定义 NTP**：每 60 秒校时一次（30 秒节流）；自定义服务器优先，
  失败回退内置；不再每分钟写 flash。
- **网页地址自动**：不再保存 IP 列表/多地址；页面只连当前 origin（`:80`→`:8080`，
  无端口自动补 `:8080`）。
- **80→8080 跳转常开**：80 绑定失败只告警，不影响 8080。
- **手机配置页**：设备内置 HTTP 服务托管单文件、零外链的移动端页面；保存后热生效。

### v3 页面/显示规则（摘要）

| 页面/状态 | 显示 | 颜色 / 字号 |
|---|---|---|
| `clock` | 本地 `HH:MM:SS`；时间无效 `--:--:--` | `colors.clock`；无效暗灰 `#404040`，scale 2 |
| `clock_countdown` | 上半时钟、下半倒计时/正计时（同一秒）；无有效目标显示 `NO COUNTDOWN` | 时钟 `colors.clock`；倒计时 `colors.countdown`/预警红；正计时 `colors.countup`；`NO COUNTDOWN` `colors.dim`；均 scale 1 |
| `countdown` | 倒计时（`<100h` scale 2；`>=100h` `DdHH:MM:SS` scale 1）；到达后正计时；无有效目标显示 `NO COUNTDOWN` | `colors.countdown`/预警红/`colors.countup`/dim |
| `datetime` | 时间 `HH:MM:SS` scale 2（baseline 10）；日期 `YYYY-MM-DD` scale 1（baseline 15） | 时间 `colors.clock`；日期 `colors.dim` |
| `live` | 每行 `<label> <value>`；整页无配置显示 `NO LIVE` | label 平台色、value 白；`--`/陈旧 `colors.dim` |
| 覆盖层 | IP（全屏 scale2 滚动，无前缀）/ `NO WIFI` / `AP ...`/`TC002-Setup` 两行 | 绿/橙/红；优先级最高 |
| 切页横幅 | `shortTitle` + `n/N` | `colors.clock`，scale 1，1.2 秒 |

- 所有可见时间带秒（`HH:MM:SS`），日期 `YYYY-MM-DD`；超宽先收紧字距、再截断；
  `sendLedData` 补到 64 列（右侧 12 列恒黑）。

## 方案要点

- 原生 App 启动后初始化 MCU、恢复系统时钟、加载/迁移配置、注册页面宿主，
  通过 `PageBase::sendLedData` 直接刷新 52×16 LED；250ms 定时器 + 脏帧判定，
  稳态约 1 帧/秒。
- **页面宿主 `PageHost`** 持有页表与当前页：翻页/横幅/覆盖层/当前页持久化；
  渲染由纯函数 `PageRenderer` 完成（5 类页面 + 横幅 + 覆盖层，可主机像素级测试）。
- **直播 `LiveService`** 单后台线程：B 站 HTTPS 直连 / 抖音快手 bridge 批量 / push；
  缓存 ≤24 条房间、切页立即刷新；网络 I/O 不持锁、不阻塞 UI。
- **B 站直连（v3.3 可用）**：镜像 `/res/lib/` 携带 OpenSSL 1.1.1w（`dlopen` 优先加载），
  用 `/res/ui/cacert.pem` 校验证书；端点 `getH5InfoByRoom` + `get_info` 兜底；无 cookie。
- **bridge**：PC/NAS 上的 `tools/live-bridge/bridge.py` 承担抖音（匿名 `ttwid`/限流）
  与快手（Playwright），B 站失败时兜底；设备只发一条明文 HTTP GET，响应 ≤4 KB。
- 内置 HTTP 服务（`0.0.0.0:8080`）托管手机编辑页与 v3 API；启动时另开 80 端口做
  302 跳转；`POST /api/config` 保存后热应用（页面 + 直播 + NTP）。
- 配置保存在 `/data/multitimer.conf`（v3）；WiFi 凭据在 `/data/multitimer.wifi`
  （v2 列表）；时钟状态在 `/data/preferences.json` 的 `clockState`；
  当前页在 `StoragePreferences["uiState"]`。
- SoftAP 由 `ConfigMode` 管理，与 STA 互斥；HTTP 线程只请求退出 AP。
- 编译产物为 `libzkgui.so`；开发期用 Wi-Fi ADB 下载调试，长期运行用
  `tools/make-image.ps1` 生成 `/res` 镜像并经官方 ADB 或 TF 卡升级。

## v3 配置与 v2→v3 迁移（摘要）

`/data/multitimer.conf` 为 **v3 JSON**（示例见
[`docs/V3-CONTRACT.md`](docs/V3-CONTRACT.md) §2 与
[`native/MultiTimer/README.md`](native/MultiTimer/README.md#配置v3)）：

- 保留 v2 字段：`tzOffsetMinutes`、`warnSeconds`、`ntpServer`、`colors`
  （新增 `dim`、**`countup`**）、`targets`；
- 新增：`live{source,url,refreshSec,timeoutMs}`、`defaultPageId`、
  `pages[]`（1..12；`pages[].type` = `clock`/`clock_countdown`/`countdown`/`live`/`datetime`；
  live 页每页 1..2 个房间）；`display` 保留兼容但**已废弃**；
- `POST /api/config` **只接受 v3**；v2 返回 400 并提示刷新页面（避免旧页面覆盖新字段）。

**v2 → v3 迁移**（`ConfigStore::load()` 读盘时一次完成并立即保存）：

- 保留 `tzOffsetMinutes`/`warnSeconds`/`colors.clock/countdown/warning`/`targets`/`ntpServer`；
- 新增 `colors.dim`、`colors.countup`（默认 `#00BFFF`）、`live`（默认 `source:"off"`）；
- 页面固定补 3 页：`p0 clock`、`p1 clock_countdown`、`p2 countdown`，
  `defaultPageId="p0"`；不自动创建 live 页；
- 全新安装默认 4 页（多一个空 live 页）；
- v1 旧配置先按 v2 规则迁移，再补 v3 页面；时间无效时暂缓迁移；
- 回滚到 v2 固件时 v3 配置不会被读取（v2 按默认配置运行），回滚前请备份。

`/data/multitimer.wifi` 仍为 v2 列表（最多 8 个，密码明文保存在设备）：

```json
{
  "version": 2,
  "networks": [
    { "ssid": "home-2g", "password": "<password>", "enabled": true }
  ]
}
```

- 旧单对象自动兼容；`GET /api/wifi` 不返回密码（只返回 `hasPassword`）；
  保存时同 SSID 留空保留原密码，新 SSID 留空按开放网络；
- 密码只提交给设备、不写入浏览器 localStorage、不写日志；仓库文档不记录真实密码。

## HTTP API（v3 摘要）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET/POST | `/api/config` | v3 配置读取/保存（POST 只收 v3，保存后热应用） |
| POST | `/api/page` | `{"id":...}`/`{"index":...}` 切当前页（不写配置）；`{"reset":true}` 清页记忆 |
| GET | `/api/live/status` | 直播数据源健康 + 全部房间快照（≤24） |
| POST | `/api/live/refresh` | 触发异步刷新（`{"page":N}` 可省） |
| POST | `/api/live` | `push` 模式推数据 |
| GET | `/api/status` | 设备/显示状态；`state.page{id,index,title,type,count}` |
| GET/POST | `/api/wifi*`、`/api/time` | 保持 v2.1.2 行为不变 |

请求体上限 16 KB、规范化配置 ≤12 KB；错误统一 `{"ok":false,"error":"..."}`；
HTTP 无鉴权/无 TLS，只应放可信局域网。完整说明见
[`native/MultiTimer/README.md`](native/MultiTimer/README.md#http-apiv3)。

已知差异（v3.0 QA 基线；详见 [`docs/QA-v3-initial.md`](docs/QA-v3-initial.md) §7）：
- 配置缺失/损坏时 `GET /api/config` 返回默认 v3 4 页（v3.0 已修复）；
- `/api/live/status` 实际字段比契约简表更全，以实际响应为准；
- B 站直连超时改用 `live.timeoutMs`；失败后 60 分钟退避（v3.3 下直连已可用）；
- v3.3 的 `mode=countup`、按键/IP 新映射、网页自动地址为增量行为，
  旧 QA 报告未覆盖（暂无 v3.3 独立 QA）。

## 构建 / 测试

安装 FlyThings IDE 2.x 后可在 IDE 中导入工程并按 `Ctrl + Alt + Z` 编译，或：

```powershell
# 交叉编译（Windows headless；v3.3 最终产物）
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 `
  -Workspace build\v3g-workspace -LogPath build\v3g-build.log

# 主机单测（211 用例）
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1

# 网页自检（90 项）
node .\native\MultiTimer\tools\check_web.js

# 改网页后重新生成内嵌 gzip 资源
python .\native\MultiTimer\tools\build_web_asset.py

# bridge 离线单测（40 用例）
cd tools\live-bridge; python -m unittest -v test_bridge; cd ..\..
```

当前结果（2026-10-22 22:02:00）：交叉编译 **0 errors / 0 warnings**
（`build/v3g-build.log`，21:49:02）；主机单测 **211/211**；网页自检 **90/90**；
bridge 单测 **40/40**。
最终源码构建产物 `build/v3g-workspace/MultiTimer/Release/libzkgui.so` 为
**722,532 字节**，SHA256 `1A4959919B126AC35CE673BF14799A83CFE767063CA61A17896E882E07B8086E`。
`dist/update.img` 由 `make-image.ps1` 在本地生成（叠加 `res-extra` 的 2 个 OpenSSL 库），
字节数/hash 见 `dist/build-info.txt` 与下节。v3.0 构建（718,436 / `A4B1D7E8...`）
为历史。详细参数、网页资源生成步骤与编译说明见原生工程 README。

## 固化与备份

- 本地开发时 `dist/` 曾生成 v3.3 update.img（2026-10-22 21:49:28，已真机固化）；公开仓库不包含该镜像：
  - `libzkgui.so` 722,532 字节，SHA256
    `1A4959919B126AC35CE673BF14799A83CFE767063CA61A17896E882E07B8086E`；
  - `update.img` 1,942,076 字节，SHA256
    `8B2BCF20A71202610FAC6CA9C2A3D339B8D457E2C48D358B7C791933114C16B4`
    （`res.tmp` 1,941,504 字节 / `92E2209AB6FAFC9456737C858D3E26DB95B4BF17F0A271C543B9C2D5C5159160`，
    8 MiB 分区 **23.15%**）；
  - 镜像额外包含 `/res/lib/libssl.so.1.1` + `libcrypto.so.1.1`（OpenSSL 1.1.1w，
    来自 `native/MultiTimer/res-extra/`，共 227 个文件）；
  - `build-info.txt`：`generated_local=2026-10-22T21:49:28`。
- 设备 `/res` 已是 v3.3：回读 `/res/lib/libzkgui.so` = 722,532 字节 /
  `1A495991...`、`/tmp` 为空、`/data` 配置保留；真机验证证据见
  [`docs/STATE-2026-10-22-v3.md`](docs/STATE-2026-10-22-v3.md) §10.2。
- 历史版本：v3.2（722,532 / `09753AAD...`，1,942,076 / `8C098B91...`）、
  v3.0（718,436 / `A4B1D7E8...`，864,828 / `7058D4C0...`）与
  v2.1.2（480,876 / `7A762407...`、762,428 / `A3C1655A...`）记录见
  [`docs/FLASH-RUNBOOK.md`](docs/FLASH-RUNBOOK.md)。
- 命令、证据、风险与回滚步骤见 [`docs/FLASH-RUNBOOK.md`](docs/FLASH-RUNBOOK.md)；
  发布包与 SHA256 见 [`dist/README.md`](dist/README.md)。
- 长按 USB-C 旁复位键恢复官方固件会**清空 `/data`**（配置、WiFi、时钟状态全部丢失），
  这是官方恢复路径的既定行为。

## 当前状态

2026-10-22 完成 v3.3 实现、主机测试、交叉编译、镜像生成、真机固化与验证：

- 页面系统：**5 类页面** + 网页页面编辑器 + 旋钮翻页 + 横幅 + 当前页持久化；
- **正计时**（目标过期后累计、`colors.countup` 默认 `#00BFFF`）+
  **无目标 `NO COUNTDOWN`**（dim）+ **datetime 页**（时间+日期）；
- 网页页面类型新增“日期 + 时间”，模拟器支持 datetime 帧；
- 直播监控：**B 站设备直连可用**（镜像 OpenSSL 1.1.1w）、抖音/快手经 bridge、
  push；`live.source` 四模式；指标 `online/likes/fans/status`；
- 配置 v3 + v2/v1 自动迁移；`POST /api/config` 只收 v3；
- API v3：`/api/page`、`/api/live/status`、`/api/live/refresh`、`/api/live`；
  `/api/status` 增加 `page` 字段；保留 WiFi/NTP/time/80 跳转；
- 主机单测 **211/211**（TimeMath 24 + TimerEngine 19 + ConfigStore 54 +
  mini_json 10 + WifiStore 19 + **PageRenderer 44** + LiveFormat 24 +
  BilibiliClient 17）；
- 网页自检 **90/90**；bridge 单测 **40/40**；
- FlyThings 交叉编译 **0 errors / 0 warnings**（最终源码产物 722,532 字节 /
  `1A495991...`，`build/v3g-build.log` 21:49:02）；
- QA：`docs/QA-v3-*.md` 只覆盖 v3.0 基线；**v3.3 暂无独立 QA 报告**（由 Lead 实现并
  真机验证）；
- **v3.3 已固化到设备 `/res` 并完成真机验证**：回读 hash 与 `dist` 一致、
  `/tmp` 为空、`/data` 保留；**NO COUNTDOWN**（无目标 p1 `text2` / p2 `text`）、
  **datetime 页**（`text=22:01:27`、`text2=2026-10-22`）、B 站设备直连（bridge 停止时
  仍 `state=ok`）、网页自动地址/类型加载、页面/配置迁移保留均通过；
- 仍待用户确认：**物理中键单击 IP / 右键长按 SoftAP 的 LED 观感**、8 WiFi 切换、
  24h+ 稳定性等（见 [`docs/STATE-2026-10-22-v3.md`](docs/STATE-2026-10-22-v3.md) §10.2）；
- 目标/直播配置由用户维护（验证用临时目标已删除、用户原有配置已恢复，
  具体以设备 `GET /api/config` 为准，本文不对具体目标/房间作断言）。

详细证据、真机验证结果与恢复命令见
[`docs/STATE-2026-10-22-v3.md`](docs/STATE-2026-10-22-v3.md)；
v2.1.2 历史快照见 [`docs/STATE-2026-10-22-v21.md`](docs/STATE-2026-10-22-v21.md)；
开发交接见 [`docs/HANDOFF-native.md`](docs/HANDOFF-native.md)。

## 仓库结构

```text
native/MultiTimer/          # 当前唯一维护的 FlyThings 原生工程
  src/timer/                # TimerTypes / TimeMath / TimerEngine（纯 C++11）
  src/time/                 # TimeKeeper：系统时钟、恢复、300 秒持久化
  src/pages/                # PageTypes / PageRenderer / PageHost / TimerPage
  src/live/                 # LiveTypes / LiveFormat / LiveCache / LiveService
  src/net/                  # HttpServer / HttpClient / TlsClient / Bilibili*
                            # ConfigMode / WiFi 自举 / SNTP / mini_json
  src/config/               # ConfigStore v3（迁移）/ WifiStore（多网络 v2）
  res-extra/lib/            # 镜像额外安装到 /res/lib 的 OpenSSL 1.1.1w（B站直连）
  third_party/openssl-armhf/# OpenSSL 1.1.1w 来源/许可/说明
  web-src/index.html        # 单文件手机配置页（页面/直播编辑/模拟器/WiFi/NTP）
  tools/                    # build_web_asset.py / check_web.js
tools/live-bridge/          # 直播数据中继（Python，B 站/抖音/快手）
docs/V3-CONTRACT.md         # v3 实现契约（冻结）
docs/DESIGN-pages-v3.md     # 页面/渲染设计
docs/DESIGN-web-v3.md       # 网页配置器设计
docs/RESEARCH-live-apis.md  # 直播接口调研（B站/抖音/快手实测）
docs/LIVE-BRIDGE.md         # bridge 协议、部署、CLI、错误码
docs/VERIFY-bridge.md       # bridge 实测与文档一致性检查
docs/STATE-2026-10-22-v3.md # v3.3 工作快照（当前）
docs/QA-v3-initial.md       # v3.0 QA 报告（历史基线）
docs/QA-v3-final.md         # v3.0 QA 最终回归（历史基线）
docs/HANDOFF-native.md      # 架构、接口、线程安全、测试与限制
docs/FLASH-RUNBOOK.md       # /res 固化流程、v2/v2.1 执行记录与回滚
docs/STATE-2026-10-22-v21.md# v2.1.2 快照（历史）
docs/STATE-2026-10-22-v2.md # v2 快照（历史，保留不动）
docs/STATE-2026-10-22.md    # v1 快照（历史，保留不动）
tools/build.ps1             # Windows headless 构建脚本
tools/make-image.ps1        # 以 stock /res 为基线生成 update.img
tools/flash-update.ps1      # 官方 ADB 固化（默认 DryRun，-Confirm 才写设备）
tools/deploy-tmp.ps1        # /tmp 临时部署（开发调试）
tools/platform-tools/       # adb 等调试工具
tests/                      # 主机单测（211 个用例）与网页自检
dist/                       # libzkgui.so、EasyUI.cfg、ui/main.ftu（完整 update.img 不入库）
dist-minimal/               # 最小 /res 镜像（不含官方 stock，可随仓库分发）
legacy/                     # 旧 AWTRIX/Python 方案存档，不再维护
LICENSE                     # GPL-3.0 许可证全文
THIRD_PARTY_NOTICES.md      # 第三方组件与字体许可声明
doc.md / doc.html           # FlyThings IDE 官方说明
repo_readme_cn.md           # Ulanzi TC002 官方仓库说明
demo_readme.md              # 官方示例 API 速查
demo_manifest.xml           # 官方示例 Manifest
tree.json                   # 官方仓库文件树
```

官方 SDK 资料（`doc.md`、`doc.html`、`repo_readme_cn.md`、`demo_readme.md`、
`demo_manifest.xml`、`tree.json`）保留在仓库根目录，未做修改。

## 许可

本项目主体代码按 **GNU General Public License v3.0 or later（GPL-3.0-or-later）**
发布，许可证全文见 [`LICENSE`](LICENSE)。本工程基于 Ulanzi 官方开源仓库 [`UlanziTechnology/Ulanzi-U-Clock-TC002`](https://github.com/UlanziTechnology/Ulanzi-U-Clock-TC002)（GPL-3.0-or-later）开发，
并复制/改写了官方示例中的 `PageBase`、`KeyManager`、`McuManager`、`TomThumb` 等文件；
官方版权声明应随衍生作品保留。

任何分发衍生作品（包括修改后的固件、Demo 或再打包版本）时，必须：

- 继续以 GPL-3.0-or-later 开放源代码；
- 保留原作者与官方 SDK 的版权声明；
- 在文档中明确标注所做修改。

仓库内第三方组件（BlueZ、Adafruit GFX 字体、FlyThings SDK 依赖包等）遵循各自
原始许可证，详见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。该声明按官方
SDK 仓库原文保留，其中的 `Z21_TC002_Demo/...` 路径指官方示例；本项目对应副本位于
`native/MultiTimer/src/`。










