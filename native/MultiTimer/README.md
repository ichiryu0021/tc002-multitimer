# MultiTimer v3.3 — Ulanzi TC002 页面系统 + 正计时/日期页 + 直播监控（FlyThings 原生）

Ulanzi TC002（52×16 RGB 像素时钟）的 **FlyThings / Z21 原生 C++ 应用**。
v2 的“正常时钟 + 带日期多目标倒计时”与 v2.1 的多 WiFi / SoftAP / 每分钟 NTP /
多时钟地址在 v3 中**全部保留**；v3 新增：

- **可翻页页面系统**：页面可在网页里增删/排序/编辑；旋钮 CW/CCW 翻页，跳过停用页；
- 五类页面：`clock`（全屏时钟）、`clock_countdown`（上半时钟 + 下半倒计时，8+8 分屏）、
  `countdown`（倒计时/正计时）、`live`（**上下两行，一页 2 个直播间**，可多页）、
  `datetime`（日期 + 时间：时间大字 + 日期小字）；
- **直播监控**：B 站走设备 **HTTPS 直连**（镜像 `/res/lib` 携带 OpenSSL 1.1.1w，
  系统 1.1.0i 不可用），抖音/快手经局域网 **bridge**（PC/NAS 上的 `tools/live-bridge`，
  明文 HTTP）；`live.source` = `off`/`bridge`/`hybrid`/`push`，`hybrid` 下 B 站直连、
  B 站失败回退 bridge；指标 `online/likes/fans/status`；平台/房间/指标可在网页调整；
- **正计时**：目标到达后从最近的过期目标累计（`HH:MM:SS`），正计时颜色
  `colors.countup` 默认 `#00BFFF`；完全没有有效目标时显示 `NO COUNTDOWN`（dim）；
- **按键与 IP**：中键**单击**显示全屏 IP（无前缀、超宽水平滚动，12s）；**右键长按 3s**
  进出 SoftAP；旋钮 CW/CCW 翻页、旋钮按键刷新直播；
- **网页地址自动**：不再保存 IP 地址列表，只用当前打开页面的 origin（自动补 `:8080`）；
  设备只保存 WiFi、IP 由 DHCP 获取；
- **当前页持久化**：重启恢复到上次停留页（节流写 flash）；
- **中键单击**显示 `IP`/`NO WIFI`（全屏滚动 12s）；**右键长按 3 秒**进出 SoftAP。

所有显示时间统一 `HH:MM:SS`，日期统一 `YYYY-MM-DD`；配置保存在设备 `/data`，
设备独立运行，不需要电脑或云端服务常开（直播数据源除外，见下文）。

> **真机状态（2026-10-22 22:02:00）**：v3.3 已完成**真机固化与验证**。
> 主机单测 **211/211**、网页自检 **90/90**、交叉编译 **0 errors / 0 warnings**
> （最终源码 `build/v3g-workspace/.../libzkgui.so`，722,532 字节 / `1A495991...`）。
> `dist/update.img`（21:49:28 生成，1,942,076 字节 / `8B2BCF20...`）已通过官方 ADB
> 固化到设备 `/res`：`/tmp` 为空、`/data` 配置保留、回读
> `/res/lib/libzkgui.so` = 722,532 字节 / `1A495991...`。
> 真机已验证：**NO COUNTDOWN**（无目标 p1 `text2` / p2 `text`）、**datetime 页**
> （`text=22:01:27`、`text2=2026-10-22`）、**B 站设备直连可用**
> （bridge 停止后示例房间仍 `state=ok`）、网页自动地址加载、页面/迁移/配置保留；
> 待用户确认项（物理中键单击 IP / 右键长按 SoftAP 的 LED 观感、8 WiFi 切换、
> 24h+ 稳定性等）见
> [`docs/STATE-2026-10-22-v3.md`](../../docs/STATE-2026-10-22-v3.md) §10.2。
> **B 站设备直连在 v3.3 已可用**（镜像携带 OpenSSL 1.1.1w），详见下文
> “B 站设备直连（v3.3 可用）”。
> 目标/直播配置由用户维护（验证用临时目标已删除、用户原有配置已恢复；
> 以设备 `GET /api/config` 为准，本文不对具体目标/房间作断言）。
> 已知差异见本文末尾“v3 已知差异”。

---

## 与官方固件的关系（重要）

**官方 SDK（FlyThings）的部署单位是整个 `libzkgui.so`，不是官方固件里的一个可安装
子应用。** 官方 TC002 固件是 AWTRIX 风格的 C++ 应用，但：

- 没有公开的“原生 App 插件”ABI；
- `DiyApp` 只是 `/data/diy` 里的图片/GIF 应用，不能执行 C++；
- 官方 HTTP/MQTT 的 `Custom App` 只能推送文字/图片/绘图，需要手机、电脑或
  Home Assistant **常开**推送，无法让时钟独立计时。

因此在**不修改官方固件源码**的前提下，只能二选一：固化本工程（替换
`libzkgui.so`，官方 App 列表/设置页不再存在），或保留官方固件但依赖常开主机推送。
本工程选择前者；`dist/update.img` 是固化镜像（当前为 v2.1.2，v3 镜像待生成）。
官方 App 与本工程**不能共存**。

---

## 功能

### v3：页面系统

- **页面列表**：1..12 页，数组顺序 = 旋钮顺序；每页有类型、标题、短标题、启用开关；
  可设默认页（`defaultPageId`）。
- **五类页面**：

  | 类型 | 屏幕内容 |
  |---|---|
  | `clock` | 本地 `HH:MM:SS`，大字号（scale 2） |
  | `clock_countdown` | 上半 0..7px 时钟、下半 8..15px 倒计时；两半均小字号（scale 1），同一秒；目标到达后下半显示**正计时**；完全没有有效目标时显示 `NO COUNTDOWN`（dim） |
  | `countdown` | 倒计时 `<100h` 大字号、`>=100h` 用 `DdHH:MM:SS` 小字号；目标到达后显示**正计时**（不再回时钟）；完全没有有效目标时显示 `NO COUNTDOWN`（dim） |
  | `datetime` | 时间 `HH:MM:SS` scale 2（baseline 10，字距 2→0 自适应）；日期 `YYYY-MM-DD` scale 1（baseline 15，dim）；时间无效 `--:--:--` |
  | `live` | **上下两行，每行一个直播间（一页 2 个）**：`<label><value>`，label 平台字母+房间尾号（可改），value 在线/点赞/粉丝/开播状态 |

- **旋钮翻页**：CW = 下一页、CCW = 上一页；跳过 `enabled=false` 页；默认循环；
  切页时显示 1.2 秒横幅（`shortTitle` + `n/N`）。
- **当前页持久化**：`StoragePreferences["uiState"]` 保存 `id,index,epoch`；
  页面稳定 30 秒且距上次写 ≥120 秒才落盘；重启恢复优先级：
  最后停留页 > `defaultPageId` > 第一页。→ 掉电最多丢最近 2 分钟的页位置。
- **旋钮按键**：当前页是直播页时立即刷新直播数据。
- **中键单击**：显示 IP/NO WIFI（全屏 scale 2、无“IP”前缀、超宽水平滚动，12s）。
- **右键长按 3s**：进入/退出 SoftAP 配网（LED 显示 SSID+固定密码两行）。
- **正计时/无目标**：目标过期后显示正计时；没有有效目标显示 `NO COUNTDOWN`（dim）。
- **datetime 页**（v3.3 新增）：日期时间页；时间大字在上半屏、日期小字在底部。
- 左键不分配。

### v3：直播监控

- **四类数据源**（`live.source`）：

  | 模式 | 行为 |
  |---|---|
  | `off`（默认） | 不产生任何网络请求；直播页显示 `--` / `NO LIVE` |
  | `bridge` | 设备每 `refreshSec` 向局域网 bridge 发 1 个明文 HTTP GET，按当前页批量取 2 个房间（三平台都由 bridge 提供） |
  | `hybrid` | **B 站设备 HTTPS 直连**（镜像携带 OpenSSL 1.1.1w）；抖音/快手走 bridge；B 站直连失败且配了地址时回退 bridge |
  | `push` | 设备不轮询；外部程序 `POST /api/live` 推送数据 |

- **平台与指标**（B 站设备直连，抖音/快手走 bridge）：
  - B 站：`online`（**人气值**，不是并发人数）、`likes`、`fans`、`status`、`title`；
    设备直连 `getH5InfoByRoom` 主端点、失败回退 `get_info`（无 `likes`）。
  - 抖音：`online`、`likes`、`fans`（19 位 `room_id` 路径）、`status`；需 bridge。
  - 快手：`online`、`likes`、`status`；bridge 需 Playwright（无浏览器时返回
    `kuaishou_requires_browser`，屏幕显示 `--`）。
- **屏幕显示**：每页 2 个槽位（上行/下行）；label ≤3 字符（平台色：B 站 `#FF6699`、
  抖音白色、快手 `#FF9900`），value ≤6 字符（`1234`/`1.2w`/`9.9y`/`--`/`LIVE`/`OFF`）；
  数值 `0` 是有效值（未开播），失败/未配置显示 `--`；整页无配置房间显示 `NO LIVE`。
- **刷新**：`refreshSec`（15..300，默认 30）轮询当前页；切页/旋钮按键/保存配置/
  WiFi 恢复后立即刷新；失败不阻塞 UI，保留旧值并按陈旧阈值变暗。
- **网页**：可编辑页面列表、每页 2 个直播槽位（平台/房间号/标签/启用/指标）、
  全局数据源与刷新参数；模拟器可预览直播两行。v3.0 网页**不含**直播监控卡；
  房间状态/错误用 `GET /api/live/status` 查询，立即刷新用 `POST /api/live/refresh`。
  直播间标题等第三方文本只按纯文本显示（不执行 HTML）。

### 保留的 v2.1.2 功能

- **正常时钟 / 带日期多目标倒计时**：最多 12 个目标，`YYYY-MM-DD` + `HH:MM:SS`；
  启用目标按 UTC 时刻升序、同刻去重、过期跳过；到点显示 `00:00:00` 后切下一个；
  无未来目标回时钟；不做正计时/响铃/闪烁。
- **严格绑定时钟**：每帧只取一次系统时钟计算剩余秒数；校时/网页对时/改时区立即变化。
- **时区分钟偏移**：`tzOffsetMinutes` -720..840，支持 `+05:30`、`+05:45`、`+12:45`；
  不依赖 tzdata、不自动处理夏令时。
- **预警**：剩余 ≤ `warnSeconds`（0..600，0=关闭）常亮红色。
- **掉电保存**：目标/时区/颜色/预警/NTP/页面/直播配置写入 `/data/multitimer.conf`；
  系统时钟每 300 秒写 `StoragePreferences["clockState"]`；无 RTC 时重启恢复到最后
  保存时刻，联网后 SNTP 校正。
- **多 WiFi 列表**：`/data/multitimer.wifi` v2 JSON，最多 8 个；系统自动连接失败后
  按列表顺序尝试；网页可增删/启停/排序/编辑；`GET /api/wifi` 只返回 `hasPassword`。
- **SoftAP 配网**：**右键长按约 3 秒**进入/退出；SSID 固定 `TC002-Setup`，
  密码固定 `12345678`（固件固定值，不是用户 WiFi 密码）；AP IP 实测
  `192.168.100.1`；AP 15 分钟无网页 API 活动自动退出；AP 与 STA 互斥。
- **每分钟 NTP / 自定义 NTP**：联网每 60 秒校时一次（30 秒节流）；自定义服务器优先，
  失败回退内置列表；`noteSynced` 不每分钟写 flash，由 `TimeKeeper::tick` 每 300 秒落盘。
- **网页地址自动**：不再保存 IP 地址列表，只用当前打开页面的 origin（`:80`→`:8080`，
  无端口自动补 `:8080`）；设备只保存 WiFi、IP 由 DHCP 获取。
- **80→8080 跳转常开**：启动同时监听 80 与 8080；80 用 `getsockname` 取本端 IP 后
  302；80 绑定失败只告警，不影响 8080。

### 显示规则（52×16，TomThumb）

| 页面/状态 | 条件 | 显示 | 颜色 / 字号 |
|---|---|---|---|
| `clock` | 时间有效 | 本地 `HH:MM:SS` | `colors.clock`（默认白），scale 2 |
| `clock` | 时间无效 | `--:--:--` | 暗灰 `#404040`，scale 2 |
| `clock_countdown` 上半 | 时间有效 | 本地 `HH:MM:SS` | `colors.clock`，scale 1 |
| `clock_countdown` 下半 | 有未来目标 | 倒计时 `HH:MM:SS` / `DdHH:MM:SS` | `colors.countdown`（默认绿）；≤`warnSeconds` 用 `colors.warning` |
| `clock_countdown` 下半 | 有已过期目标、无未来目标 | 正计时 `HH:MM:SS` / `DdHH:MM:SS` | `colors.countup`（默认 `#00BFFF`） |
| `clock_countdown` 下半 | 完全没有有效目标 | `NO COUNTDOWN` | `colors.dim`（默认 `#808080`） |
| `countdown` | 有未来目标 | 倒计时（<100h scale 2；≥100h `DdHH:MM:SS` scale 1） | 绿 / 预警红 |
| `countdown` | 有已过期目标、无未来目标 | 正计时（<100h scale 2；≥100h scale 1） | `colors.countup` |
| `countdown` | 完全没有有效目标 | `NO COUNTDOWN`（scale 1，基线 10） | `colors.dim` |
| `datetime` | 时间有效 | 时间 `HH:MM:SS` scale 2（baseline 10）+ 日期 `YYYY-MM-DD` scale 1（baseline 15） | 时间 `colors.clock`；日期 `colors.dim` |
| `datetime` | 时间无效 | `--:--:--`（基线 10） | 暗淡灰 |
| `live` 每行 | 已配置且有数据 | `<label> <value>` | label 平台色、value 白色 |
| `live` 每行 | 失败/无数据/陈旧 | `--` 或旧值 | `colors.dim` |
| `live` 整页 | 无已配置房间 | `NO LIVE` | `colors.dim` |
| 覆盖层 | 中键短按 / 首次联网 / AP | `IP x.x.x.x`、`NO WIFI`、`AP ...`、`TC002-Setup` 两行 | 绿 / 橙 / 红，scale 1 |

- 覆盖层优先级最高；切页横幅（1.2s）次之；然后才是页面内容。
- **IP 覆盖层**：全屏 scale 2、无“IP”前缀、超宽水平滚动（60ms/像素），显示 12s；
  `NO WIFI` 仍为两行小字 6s；AP 进入时显示 SSID+固定密码两行 10s。
- 预警为整段常亮红色，不闪烁；所有可见时间带秒。
- 文本超宽先收紧字距、再按 52 像素截断；`sendLedData` 把每行补到 64 列
  （右侧 12 列恒黑）。

---

## 时间语义（重要）

设备没有 RTC、没有 tzdata，系统时区固定 UTC（`sysclktz 0`）。因此：

1. **时间源**：设备系统时钟（UTC epoch），统一由 `TimeKeeper` 封装。
2. **有效时间**：`now >= 1700000000`（2023-11-14）。无效时 LED 显示
   `--:--:--`，`/api/config` 仍可访问，网页可用浏览器时间对时。
3. **本地时间**：`local = utc + tzOffsetMinutes × 60`；只支持分钟偏移，不自动 DST。
4. **目标解释**：目标存本地日期 + 本地墙上时间；改时区后同一
   `YYYY-MM-DD HH:MM:SS` 会按新时区换算成新的 UTC 时刻。
5. **到期/过期**：目标 epoch < now 视为过期跳过；epoch == now 显示 `00:00:00`；
   下一秒切下一个未来目标；没有未来目标时回时钟。
6. **SNTP**：链路建立后立即尝试；在线每 30 秒检查节流，距上次成功 ≥60 秒才发下一轮
   （最多每分钟一次）；自定义 `ntpServer` 优先，失败回退内置。
7. **网页对时**：加载时设备时间无效或与浏览器差 >2 秒自动 `POST /api/time`；
   保存配置成功后静默对时一次；也可点“立即对时”。
8. **持久化**：`setUtc()` 立即写 `clockState`；SNTP 成功只 `noteSynced()` 更新内存，
   `TimeKeeper::tick()` 每 300 秒统一落盘；首次从无效变有效立即保存。
9. **重启恢复**：`TimeKeeper::restore()` 在启动早期调用；恢复出来的只是“上次保存时刻”
   （不是关机时刻），联网后 SNTP 校正。
10. **严格绑定**：`PageHost::tick()` 每帧只取一次 `time(NULL)`，时钟/倒计时/分屏两半
    共用同一个 `now`，禁止缓存递减。

---

## 直播配置与部署（B 站直连 / 抖音快手 bridge）

### 网页配置（推荐）

1. 手机打开 `http://<设备IP>:8080/`（或 `http://<设备IP>/`，80 自动跳到 8080）；
2. 在「页面」卡片新增/编辑 `live` 页，每页填 **2 个直播间**：平台、房间号、标签（可选）、
   启用、指标（可“跟随页默认”）；
3. 在「页面 → 直播数据源」选择：
   - `hybrid`（推荐）：**B 站设备 HTTPS 直连**（镜像携带 OpenSSL 1.1.1w），
     抖音/快手走 bridge，需填 bridge 地址，如 `http://192.168.31.10:8081/live`；
     B 站直连失败时回退 bridge；
   - `bridge`：三平台都走 bridge；
   - `push`：外部程序推送；`off`：关闭。
4. `refreshSec` 默认 30 秒（15..300）；`timeoutMs` 默认 3000ms（1000..5000）；
5. 保存后设备热应用，当前直播页立即刷新一次。

房间号形式：

| 平台 | 形式 | 例子 |
|---|---|---|
| B 站 | 数字短号/真号（1..20 位） | `6`、`21452505` |
| 抖音 | 12 位左右 `web_rid`（分享链接数字） | `611063790284` |
| 抖音 | 19 位数字 `room_id`（reflow 端点，更宽松） | `7076284313080843019` |
| 快手 | `principalId`（`live.kuaishou.com/u/<id>`） | `3xwnf96yetb5ewm` |

### bridge 部署（抖音/快手必需；B 站直连失败时兜底）

```powershell
cd tools\live-bridge
python -m pip install -r requirements.txt
python bridge.py                     # 默认 0.0.0.0:8081
```

- 设备与 PC/NAS 必须在同一网段；防火墙放行 TCP 8081（仅私有网络）；
- 设备 `live.url` 只接受 `http://`（设备端不支持 https bridge）；
- 协议没有鉴权，**不要做公网映射**；
- 快手需要 Playwright + Chromium（可选，约 200–400 MB 内存）：
  `python -m pip install playwright && python -m playwright install chromium`；
- 完整参数、错误码、限流/缓存、systemd/Docker 见
  [`docs/LIVE-BRIDGE.md`](../../docs/LIVE-BRIDGE.md)，实测记录见
  [`docs/VERIFY-bridge.md`](../../docs/VERIFY-bridge.md)。

### B 站设备直连（v3.3 已可用）

- **镜像携带 OpenSSL 1.1.1w**：`native/MultiTimer/res-extra/lib/` 的
  `libssl.so.1.1`（352,540 B）+ `libcrypto.so.1.1`（1,737,128 B）随镜像安装到
  `/res/lib/`；来源/许可/兼容性见
  [`third_party/openssl-armhf/README.md`](third_party/openssl-armhf/README.md)。
- `TlsClient` 优先 `dlopen` `/res/lib` 的 1.1.1w（`MT_TLS_LIB_DIR` 仅测试用），
  再回退系统库；系统 1.1.0i 的 TLS client method 被裁掉（`SSL_connect` 141640BF），
  实际不可用。
- 主机 `api.live.bilibili.com`，CA `/res/ui/cacert.pem`（145 张根证书）、TLS 1.2 +
  SNI + 主机名校验；无 cookie、无登录、无签名；主端点 `getH5InfoByRoom`，
  失败回退 `get_info`（没有 `likes`）；`code != 0` 视为房间失败。
- **真机证据**：停止 bridge 后，B 站示例房间仍 `state=ok`（HTTPS 直连取到数据）。
- 失败时：`live.url` 非空则同轮回退 bridge，并进入 60 分钟退避后重试直连。

---

## 架构

```text
上电
 └─ mainActivity（FlyThings UI）
     └─ onUI_init()
         ├─ SystemProperties::setString("sys.zkapp.state", "running")  // 防砖，最先
         ├─ McuManager 初始化 /dev/ttyS1@1500000、查询 MCU 版本（不查询则 LED 不亮）
         ├─ TimeKeeper::restore()                  // 无 RTC：恢复最后保存时钟
         ├─ 注册 TimerPage；TimerPage::restoreUiState()（读 uiState）
         ├─ ConfigStore::load(now, &migrated) → PageHost::setConfig()
         │    ├─ v1/v2 迁移成功则立即 save() 为 v3（只写一次）
         │    └─ SntpClient::setServer(ntpServer)、LiveService::setConfig()
         ├─ LiveService::start()（单后台线程）
         ├─ PageManager::navigateTo("TimerPage")
         ├─ KeyManager：中键单击 IP；右键长按 3s SoftAP；旋钮 CW/CCW 翻页；旋钮按键刷直播
         ├─ ConfigMode 就绪 / WifiBootstrap 连上 → 覆盖层提示
         ├─ HttpServer::start(8080, applyCallback)（另试监听 80 做 302）
         └─ startWifiBootstrap()（后台：自动连接/列表兜底/每分钟 SNTP/掉线重连）

每 250ms（onUI_Timer id=1）：
  TimerPage::tick() → PageHost::tick()
     ├─ 本帧唯一一次 time(NULL) → TimerEngine::evaluate(now)
     ├─ live 页：解锁后取 LiveService 快照
     ├─ 脏帧才渲染（PageRenderer）→ sendLedData（SPI）
     └─ uiState 节流落盘（稳定 ≥30s 且距上次 ≥120s）
每 4 tick（=1s）：TimeKeeper::tick()、ConfigMode::tick()、补迁移、长按检测
```

| 模块 | 文件 | 职责 |
|---|---|---|
| 入口 / Lifecycle | `src/Main.cpp`、`src/logic/mainLogic.cc` | 初始化、250ms 定时器、按键路由、启动编排 |
| 页面模型 | `src/pages/PageTypes.h`、`src/timer/TimerTypes.h` | 页面/直播/指标/数据源枚举与配置结构 |
| 渲染 | `src/pages/PageRenderer.*` | 纯函数：5 种页面（含正计时/NO COUNTDOWN/datetime）、横幅、IP 滚动覆盖层（像素级可单测） |
| 页面宿主 | `src/pages/PageHost.*`、`TimerPage.*` | 页表/翻页/横幅/覆盖层/当前页持久化/单帧时间源；TimerPage 只做 SPI 适配 |
| 直播格式化 | `src/live/LiveTypes.h`、`LiveFormat.*` | 数值压缩（w/y）、status、平台色、label |
| 直播缓存 | `src/live/LiveCache.*` | ≤24 条、状态机、快照 |
| 直播服务 | `src/live/LiveService.*` | 单后台线程：B 站 HTTPS 直连 / 抖音快手 bridge 批量 / push、刷新调度 |
| HTTP 客户端 | `src/net/HttpClient.*`、`TlsClient.*`、`BilibiliClient.*`、`BilibiliParse.*` | 明文 HTTP、dlopen TLS、B 站取数与解析 |
| 日历/倒计时 | `src/timer/TimeMath.*`、`TimerEngine.*` | 日历/时区/格式、目标排序去重/阶段/预警 |
| 系统时钟 | `src/time/TimeKeeper.*` | 恢复/有效性/对时/300s 持久化 |
| 持久化 | `src/config/ConfigStore.*`、`WifiStore.*` | v3 校验/原子保存/迁移；WiFi v2 列表 |
| 配网 | `src/net/ConfigMode.*` | SoftAP 进入/退出、15 分钟超时、AP IP |
| 网络 | `src/net/WifiManager_task.*`、`SntpClient.*` | WiFi 自举/列表尝试/每分钟 SNTP |
| HTTP 服务 | `src/net/HttpServer.*` | 内嵌手机页、v3 API、80→8080 |
| 手机页 | `web-src/index.html` | 单文件零外链：页面/直播编辑、数据源、模拟器、WiFi/NTP；地址自动（origin） |
| 内嵌资源 | `src/web/editor_gz.h` | 由 `web-src` 生成（构建产物，勿手改） |

---

## 配置（v3）

### `/data/multitimer.conf`

```json
{
  "version": 3,
  "tzOffsetMinutes": 480,
  "warnSeconds": 10,
  "ntpServer": "",
  "colors": {
    "clock": "#FFFFFF",
    "countdown": "#00FF00",
    "countup": "#00BFFF",
    "warning": "#FF2020",
    "dim": "#808080"
  },
  "display": { "splitBottomWhenIdle": "date" },
  "targets": [
    { "date": "2026-10-23", "time": "19:25:00", "enabled": true }
  ],
  "live": {
    "source": "hybrid",
    "url": "http://192.168.31.10:8081/live",
    "refreshSec": 30,
    "timeoutMs": 3000
  },
  "defaultPageId": "p0",
  "pages": [
    { "id": "p0", "type": "clock", "title": "时钟", "shortTitle": "CLOCK", "enabled": true },
    { "id": "p1", "type": "clock_countdown", "title": "时钟+倒计时", "shortTitle": "CLOCK+CD", "enabled": true },
    { "id": "p2", "type": "countdown", "title": "倒计时", "shortTitle": "", "enabled": true },
    { "id": "p3", "type": "live", "title": "直播", "shortTitle": "LIVE", "enabled": true,
      "metric": "online",
      "rooms": [
        { "platform": "bilibili", "room": "", "label": "", "enabled": true, "metric": "" },
        { "platform": "douyin", "room": "", "label": "", "enabled": true, "metric": "" }
      ] }
  ]
}
```

### 校验规则（摘要）

| 字段 | 规则 |
|---|---|
| `version` | 必须为 `3`；`POST /api/config` 只收 v3（v2 返回 400 提示刷新页面） |
| `tzOffsetMinutes` | 必填整数 -720..840 |
| `warnSeconds` | 0..600，默认 10（0 = 关闭预警） |
| `ntpServer` | 空或 host / host:port，≤64；不支持裸 IPv6 |
| `colors` | `clock`/`countdown`/`countup`/`warning`/`dim`，`#RRGGBB`；缺省按默认值（countup 默认 `#00BFFF`） |
| `display.splitBottomWhenIdle` | **已废弃**：保留解析/回写兼容，网页已移除该设置；渲染忽略 `blank`，无目标固定 `NO COUNTDOWN` |
| `targets` | ≤12；非法单项丢弃；超过 12 项整份拒绝 |
| `live.source` | `off`/`bridge`/`hybrid`/`push`；`hybrid` 下 B 站直连、抖音/快手走 bridge，B 站失败回退 bridge（60 分钟退避） |
| `live.url` | 只接受 `http://`，≤160，无空格；bridge 用于抖音/快手（B 站失败时兜底） |
| `live.refreshSec` | 15..300，默认 30 |
| `live.timeoutMs` | 1000..5000，默认 3000 |
| `pages` | 1..12 项；未知 `type` 拒绝；`type` = `clock`/`clock_countdown`/`countdown`/`live`/`datetime`；数组顺序 = 旋钮顺序 |
| `pages[].id` | `[a-z0-9_-]{1,8}` 唯一；非法/重复自动生成 |
| `pages[].title` | ≤16 字符（可中文，仅网页/API） |
| `pages[].shortTitle` | ≤8 可打印 ASCII，切页横幅用；空则类型短名 |
| `live` 页 `rooms` | **1..2 项**（一页 2 个直播间）；`platform`/`room`(≤40)/`label`(≤3)/`enabled`/`metric` |
| `defaultPageId` | 必须引用存在页面；非法回退第一页 |
| 请求体 | 16 KB；规范化配置 ≤12 KB（超限 413 `config too large`） |

- 保存使用 `.tmp` + `fflush`/`fsync`/`fclose` + `rename` 原子替换；多线程保存互斥。
- 配置缺失/损坏时设备用默认配置运行（新装 4 页，含 1 个空直播页），接口仍可用。

### v2 → v3 迁移

- **只在 `ConfigStore::load()` 读盘时执行一次**；`POST /api/config` 不接受 v2/v1。
- 保留 `tzOffsetMinutes`、`warnSeconds`、`colors.clock/countdown/warning`、
  `targets`、`ntpServer`；新增 `dim`、`countup`（缺省 `#00BFFF`）、`display`、`live`；
- 页面固定补 3 页：`p0 clock`、`p1 clock_countdown`、`p2 countdown`；
  `defaultPageId="p0"`；不自动创建 live 页；迁移成功后立即写回 v3（只写一次）。
- 全新安装（无配置）默认 4 页（多一个空 live 页），可在网页增删。
- v1 旧配置（`tzOffset`/`points`）先按 v2 规则迁移，再补 v3 页面；
  时间无效时暂缓迁移，等时间有效后补做。
- 回滚到 v2 固件时 v3 配置不会被 v2 读取（v2 会按默认配置运行），回滚前请备份。

### v1 → v2 迁移（历史规则，仍保留）

- v1 的 `tzOffset`（小时 -12..14）→ `tzOffsetMinutes = ×60`；
- 旧 `points`（≤8 项，小时/分钟精度）→ **迁移当天**的一次性目标（时间无效则暂缓）；
- 迁移成功后立即保存（只写一次）；旧倒计时/预警颜色沿用，时钟颜色用默认白。

---

## HTTP API（v3）

监听 `0.0.0.0:8080`，每连接独立线程，**无鉴权、无 TLS、无限流**，只应放在可信局域网。
请求头 2 KB、请求体 **16 KB**、收发超时 3 秒、最多 8 个并发连接。
启动时另试监听 `0.0.0.0:80`，用 `getsockname` 取本端 IP 后 302 到 `:8080`
（80 失败只告警，不影响 8080；AP 下手机只输 IP 时走该跳转）。

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/`、`/index.html` | 内嵌手机页（gzip） |
| GET | `/api/config` | `{ok,config,device,ui,live}`；`config` 永远 v3；`device.page` 与 `ui` 同义（web-v3 兼容） |
| POST | `/api/config` | v3 schema（可 `{"config":{...}}` 包裹）→ 校验 → 保存 → 热应用；v2/v1 400；成功返回 `{ok,ui,page}` |
| POST | `/api/page` | `{"id":"p1"}`/`{"index":1}` 切页（不写配置，返回 `{ok,index,pageCount,page}`）；`{"reset":true}` 清当前页记忆 |
| GET | `/api/live/status` | 直播数据源健康 + 全部已配置房间快照（≤24） |
| POST | `/api/live/refresh` | 触发异步刷新（`{"page":N}` 可省）；返回 `{ok,queued,page}` |
| POST | `/api/live` | `push` 模式推数据（body 同 bridge 响应）；返回 `{ok,updated,ts}` |
| POST | `/api/time` | `{"epoch": 1700000000..4102444800}` 立即写系统时钟并持久化 |
| GET | `/api/wifi` | 当前网络 + 网络列表；只返回 `hasPassword`，**不返回密码** |
| POST | `/api/wifi/list` | 保存完整网络列表（≤8）并异步重连；AP 下先退出 AP |
| POST | `/api/wifi` | 兼容旧页面单网络接口；空密码不清空原密码 |
| GET | `/api/status` | `{ok,device,state}`；`state` 含 `mode`（含 `countup`）/`text2`/`banner`/`overlay`/`page{...}` |
| OPTIONS | `*` | CORS 204 |
| GET | `/favicon.ico` | 204 |
| 其它 | — | 404 `{"ok":false,"error":"not found"}` |

- `GET /api/config` 的 `ui` 是当前页只读信息
  （`pageId/pageIndex/pageCount/pageTitle/pageType`）；`live` 是数据源摘要
  （`source/refreshSec/reachable/lastOkUtc/error`）。两者都不被 POST 接受；
  `device.page`（`{id,index,title,type,count}`）是同一页信息的兼容别名。
- `GET /api/status` 的 `state.page`：`{id,index,title,type,count}`；
  `state.mode`：`clock|countdown|countup|invalid`（页面计时部分）；
  `text`/`text2`：datetime 页为 `text=HH:MM:SS`、`text2=YYYY-MM-DD`；无目标时
  countdown 页 `text=NO COUNTDOWN`、分屏页 `text2=NO COUNTDOWN`；
  `state.overlay=true` 表示中键单击的 IP/NO WIFI 覆盖层正在显示。
- **网页 v3.3 实际调用**：`GET/POST /api/config`、`POST /api/time`、
  `GET /api/wifi`、`POST /api/wifi/list`、`POST /api/page`；
  `/api/live/status`、`/api/live/refresh` 仍为固件 API（网页不调用，供脚本/排查）。
- 错误统一 `{"ok":false,"error":"..."}`；保存失败 500、连接超限 503、
  请求体/配置超限 413。
- 配网模式只能由**右键长按 3s**进入/退出，**没有 HTTP 开关接口**。
- 手机页的“立即对时”使用浏览器时间；浏览器不准，设备也会跟着不准。
- 所有时间/日期字段格式：`HH:MM:SS`、`YYYY-MM-DD HH:MM:SS`、`YYYY-MM-DD`。
- **已知差异**：`/api/live/status` 实际字段为
  `{ok,source{kind,url,reachable,lastAttemptUtc,lastOkUtc,error},refreshSec,
  timeoutMs,activePage,rooms[]}`，比契约简表更全，以实际响应为准；
  v3.0 QA 报告（[`../../docs/QA-v3-initial.md`](../../docs/QA-v3-initial.md)）
  为历史基线，其中“B 站直连不可用”的结论已被 v3.3 取代，且未覆盖 `mode=countup`、
  新按键/IP、网页自动地址等增量行为。

### `device` 字段（`/api/config` 与 `/api/status`）

```json
{
  "ip": "192.168.31.124",
  "ssid": "<当前 WiFi 名称>",
  "wifi": true,
  "uptimeSec": 715,
  "timeUtc": 1792668766,
  "timeValid": true,
  "tzOffsetMinutes": 480,
  "localTime": "2026-10-22 19:32:46",
  "lastSyncUtc": 1792668700,
  "apMode": false,
  "apSsid": "TC002-Setup",
  "apIp": "",
  "page": {"id": "p0", "index": 0, "title": "时钟", "type": "clock", "count": 4}
}
```

- `lastSyncUtc` 是最近一次 SNTP/网页对时的 epoch；仅从 flash 恢复时可能为 0。
- `apMode` 表示 AP 是否开启；`apSsid` 固定 `TC002-Setup`（未开启也返回）；
  `apIp` 仅 AP 就绪后非空（实测 `192.168.100.1`）。
- `page` 是 v3 新增的当前页信息（与 `GET /api/config` 的 `ui` 同义）；
  `device` 的旧 v2 字段含义不变。

---

## SoftAP 配网（v3.3：右键长按 3 秒）

1. 在设备上**右键长按约 3 秒**，LED 先显示 `TC002-Setup` 与固件固定密码两行约 10 秒，
   随后 SoftAP 启动；
2. 手机连接 `TC002-Setup`，密码 **`12345678`**（固件固定值）；
3. AP 就绪后设备 IP 为 **`192.168.100.1`**（实测）；手机访问
   `http://192.168.100.1/`（80）会 302 到 `:8080`；
4. 在页面里编辑 WiFi 列表并保存；设备先退出 AP，再按新列表重连 STA；
5. 再次右键长按 3 秒退出 AP（`AP OFF`），恢复 STA 自举；
   中键单击可随时显示当前 IP/AP IP（全屏滚动 12s）。

> **验证状态**：物理右键长按进入/退出 SoftAP 的 LED 观感**仍需用户现场确认**
> （中键长按在 v2.1.2 是旧入口，v3.3 已改为右键长按）。
> AP IP 与 80→8080 跳转已在开发期/真机实测。AP 密码是固定值，
> 15 分钟窗口内附近知道该密码的人也能连上配网页；没有真实用户 WiFi 密码写入仓库/文档。

### 约束

- **AP 与 STA 不能同时工作**：进入 AP 会释放 STA/DHCP；AP 期间不能上网，
  NTP/网页对时可能失败；保存列表会先退出 AP 再重连。
- AP 默认 **15 分钟**无网页 API 活动自动退出；配置/状态/WiFi 请求会续期
  （打开静态页不算活动）。
- 配网模式只能由**右键长按 3s**进入/退出，没有 HTTP 开关。

---

## 目录

```text
native/MultiTimer/
├── Manifest.xml              # Z21 平台 + 依赖包
├── .project / .cproject      # FlyThings/Eclipse 工程
├── ui/main.ftu               # 空白主 Activity
├── src/
│   ├── Main.cpp
│   ├── activity/             # IDE 生成，勿手改
│   ├── logic/mainLogic.cc
│   ├── timer/TimerTypes.h, TimeMath.*, TimerEngine.*
│   ├── time/TimeKeeper.*
│   ├── pages/PageBase.*, PageTypes.h, PageRenderer.*, PageHost.*, TimerPage.*
│   ├── live/LiveTypes.h, LiveFormat.*, LiveCache.*, LiveService.*
│   ├── config/ConfigStore.*, WifiStore.*
│   ├── net/WifiManager_task.*, SntpClient.*, HttpServer.*, ConfigMode.*,
│   │       HttpClient.*, TlsClient.*, BilibiliClient.*, BilibiliParse.*, mini_json.*
│   ├── managers/McuManager.*, KeyManager.*, PageManager.*
│   ├── utils/                # Surface / Painter / GpioHelper 等
│   ├── Fonts/TomThumb.h
│   └── web/editor_gz.h       # 由 web-src 生成的手机页 gzip 字节
├── res-extra/lib/            # 随镜像安装到 /res/lib 的 OpenSSL 1.1.1w（B站直连）
├── third_party/openssl-armhf/# OpenSSL 1.1.1w 来源/许可/说明
├── web-src/index.html        # 手机编辑页源文件（v3.3）
└── tools/
    ├── build_web_asset.py    # index.html -> src/web/editor_gz.h
    └── check_web.js          # 网页逻辑自检（90 项）
```

---

## 编译

### 前置

1. 安装 **FlyThings IDE 2.x**。本机安装路径 `D:\zkswe\FlyThingsIDE`，
   `bin\ide.exe`；
2. 工程使用 **Z21** 平台，依赖在 `Manifest.xml` 声明：
   `easyui`、`log`、`zkhardware`、`zknet`、`base-utility`、`transfer-protocols`、
   `audio-utility`、`ffmpeg`、`z`、`base-json`；
3. 依赖缓存于 `D:\zkswe\FlyThingsIDE\bin\.dep`；换机器/升级依赖需联网。
4. v3.3 的 B 站直连**不新增链接依赖**：镜像携带的 OpenSSL 1.1.1w 由运行时
   `dlopen("/res/lib/libssl.so.1.1")` 加载（`-ldl` 已在工程链接参数中），
   不改 `.cproject`；`res-extra/` 的 2 个库由 `make-image.ps1` 自动装入镜像。

### IDE 编译

1. `文件` → `导入` → `常规` → `现有项目到工作空间中`，选择 `native/MultiTimer`；
2. 右键工程 → **更新依赖**；
3. 选中工程按 **Ctrl + Alt + Z** 或工具栏绿色三角；
4. 产物：`Release/libzkgui.so`。

> `src/activity/` 由 IDE 根据 `ui/*.ftu` 自动生成，不要手改。

### Headless 编译（Windows，推荐）

仓库根目录：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1
```

默认参数：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `-ProjectDir` | `native/MultiTimer` | 工程目录 |
| `-Workspace` | `build/ide-workspace` | 独立 IDE workspace |
| `-Configuration` | `Release` | 构建配置 |
| `-LogPath` | `build/headless-build.log` | 完整构建日志 |

脚本重建 workspace、复制工程后执行 headless build；**以最终是否生成非空
`libzkgui.so` 判定成功**，不因 clean 的退出码失败。并行构建请给每个任务独立
workspace 和日志：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 `
  -Workspace build\my-workspace -LogPath build\my-build.log
```

v3.3 最终源码构建记录：
`Build Finished. 0 errors, 0 warnings.`（2026-10-22 21:49:02，
`build/v3g-build.log`），
产物 `build/v3g-workspace/MultiTimer/Release/libzkgui.so` 为 **722,532 字节**，
SHA256 `1A4959919B126AC35CE673BF14799A83CFE767063CA61A17896E882E07B8086E`
（含正计时/按键/IP/网页自动地址等 v3.3 改动）。v3.0 构建
（718,436 字节 / `A4B1D7E8...`）为历史；`dist/` 镜像值以
`dist/build-info.txt` 为准。

### 修改手机页之后

设备运行时读取 `src/web/editor_gz.h`（预压缩字节），不是 `web-src/index.html`。
改网页后必须重新生成并自检：

```powershell
python .
ative\MultiTimer\tools\build_web_asset.py
node .
ative\MultiTimer\tools\check_web.js
```

然后再编译原生工程。`editor_gz.h` 是构建产物，不要手改。

### 主机单测

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1
```

当前 **211/211** 通过（详见 [`tests/README.md`](../../tests/README.md)）。

---

## 部署与固化

### 方式一：Wi-Fi ADB 下载调试（`/tmp`，不固化）

设备与电脑同一局域网，ADB 可连（TC002 Wi-Fi ADB，默认 5555）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy-tmp.ps1 -DeviceIp 192.168.31.124
```

脚本把 `dist/EasyUI.cfg`（`/tmp` 路径）、`dist/lib/libzkgui.so`、`dist/ui/main.ftu`
推到 `/tmp/{EasyUI.cfg,lib,ui}`，再 `setprop ctl.restart zkswe`。
**不会写 `/res`，断电/重启即恢复 `/res` 中的版本。**

> ⚠️ 不要用 `adb push` 直接覆盖正在运行的 `/tmp/lib/libzkgui.so`（进程已 mmap）；
> `deploy-tmp.ps1` 用 `.new` + `rm` + `mv` 规避。`-Restore` 只删除 `/tmp` 覆盖层，
> **不会恢复官方固件**；恢复官方固件只能用 USB-C 旁复位键（会清空 `/data`）。

`/tmp` 调试注意：部署使用 `dist/EasyUI.cfg` + `dist/lib/libzkgui.so`；
`dist/lib/libzkgui.so` 已是 v3.3（722,532 字节 / `1A495991...`），可直接部署；
但 `/tmp` 不会带 `res-extra` 的 OpenSSL 库，B 站直连要在 `/res` 固化后才可用。

### 方式二：官方 ADB 固化 `/res`（发布方式）

```powershell
# 1) 生成镜像（stock /res 225 文件为基线；替换 .so 与 main.ftu，
#    并叠加 native/MultiTimer/res-extra 的 2 个 OpenSSL 库；v3.3：227 文件）
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\make-image.ps1 `
  -SoPath build\v3g-workspace\MultiTimer\Release\libzkgui.so `
  -MainFtu native\MultiTimer\ui\main.ftu `
  -OutDir dist

# 2) 只读预检（不 push / 不 setprop / 不重启）
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 `
  -Image dist\update.img

# 3) 人工复核后执行
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 `
  -Image dist\update.img -Confirm
```

脚本内部的官方流程：

```bash
adb push ./update.img /tmp/update.img
adb shell setprop sys.zkupgrade.flag 255
adb shell setprop sys.zkupgrade.dir /tmp
adb shell setprop ctl.restart zkswe
```

然后等待 ADB 断开/恢复、检查 `/res` 关键文件、轮询 `http://<ip>:8080/api/status`。
超时默认 300 秒；超时不会自动回滚，也不会自动再刷一次。
**当前设备固化为 v3.3（`dist/update.img` 2026-10-22 21:49:28 生成并刷入；
`/tmp` 为空、`/data` 保留、回读 `/res/lib/libzkgui.so` = 722,532 字节 /
`1A495991...`；`/res/lib` 含 OpenSSL 1.1.1w）。v3.0/v2.1.2 为历史版本。**

### 方式三：TF 卡升级（可选）

把 `update.img` 放到 FAT32 TF 卡根目录，插卡重新上电，在官方升级界面勾选升级；
完成后及时拔卡。该路径未作为本轮主路径。

### 恢复官方固件

**按住 USB-C 旁的复位键约 5 秒再上电**，设备会刷回官方固件，同时**清空 `/data`**
（WiFi 凭据、`multitimer.conf`、时钟状态全部丢失）。恢复的官方 App 版本可能比原来的
1.1.3 更旧；恢复后需重新配网、重新刷入本工程。

**不要**把 `build/device-backup/res-stock.img` 当 `update.img` 刷入（8 MiB 原始分区
快照，没有 572 字节升级包头，官方升级器不接受）。

### 固化后的升级

已固化设备更新版本时，重新生成 `dist/update.img` 并再次走官方 ADB/TF 卡流程；
`deploy-tmp.ps1` 只用于开发期 `/tmp` 覆盖调试，不能替代 `/res` 升级。

### 镜像与产物（当前发布：v3.3）

| 文件 | 大小 | SHA256 |
|---|---:|---|
| `dist/lib/libzkgui.so`（v3.3 最终） | 722,532 | `1A4959919B126AC35CE673BF14799A83CFE767063CA61A17896E882E07B8086E` |
| `dist/update.img`（v3.3，已固化） | 1,942,076 | `8B2BCF20A71202610FAC6CA9C2A3D339B8D457E2C48D358B7C791933114C16B4` |
| `dist/ui/main.ftu` | 162 | `CFD891ABA792AB2B73681A78A5FA09D69BF7C5A54857EC038493DD3958A6D799` |
| `dist/EasyUI.cfg`（`/tmp` 部署用） | 399 | `ECF97A876D7041C57E2E700735FEB4971868A529372E21268022692777821543` |

- `update.img = res.tmp (1,941,504) + 572` 字节包头，平台码 `0x000606`；payload
  1,941,504 字节（8 MiB `/res` 分区的 **23.15%**）。
- 镜像共 **227** 个文件 = stock 225 + `res-extra` 2；替换 `lib/libzkgui.so`、
  `ui/main.ftu`，并**新增** `/res/lib/libssl.so.1.1`（352,540 B）+
  `/res/lib/libcrypto.so.1.1`（1,737,128 B）用于 B 站直连。
- `dist/build-info.txt`：`generated_local=2026-10-22T21:49:28`，
  `extra_files=2`、`staged_files=227`、`res_tmp_sha256=02A3B3FA...`、
  `update_img_sha256=8B2BCF20...AD59`。
- 历史版本：v3.0（718,436 / `A4B1D7E8...`、864,828 / `7058D4C0...`）、
  v2.1.2（480,876 / `7A762407...`、762,428 / `A3C1655A...`）见
  [`docs/FLASH-RUNBOOK.md`](../../docs/FLASH-RUNBOOK.md) §11/§12/§13。
- `dist/EasyUI.cfg` 是 `/tmp` 临时部署配置（399 字节），**不是**镜像内的
  `/res/etc/EasyUI.cfg`（stock 318 字节）；详见 [`dist/README.md`](../../dist/README.md)。

---

## 首次 WiFi

### 凭据优先级

设备启动后先等系统已保存网络自动连接（约 15 秒）；失败后按以下顺序读取：

1. `/data/multitimer.wifi` —— 本应用的 v2 JSON 网络列表（**最高**，≤8 个）
2. `/data/misc/wifi/wpa_supplicant.conf`
3. `/data/setting.ini`
4. `/mnt/usb1/test.cfg`（U 盘根目录）
5. `/mnt/extsd/test.cfg`（TF 卡根目录）

`/data/multitimer.wifi` 为合法空列表（`networks: []`）时视为“显式没有网络”，
不再回退；删除该文件才回退后续来源。

### 多 WiFi 列表

```json
{
  "version": 2,
  "networks": [
    { "ssid": "home-2g", "password": "<password>", "enabled": true },
    { "ssid": "office", "password": "", "enabled": false }
  ]
}
```

- 最多 8 个；按文件顺序尝试，`enabled:false` 跳过；读取时同 SSID 去重保留第一个。
- 旧单对象格式自动兼容；密码明文保存在设备；`GET /api/wifi` 只返回 `hasPassword`。
- 同 SSID 空密码 = 保留原密码；新 SSID 空密码 = 开放网络。
- 保存是整表覆盖；`networks: []` 是合法的“清空”。
- 保存后设备异步重连；网页约 2.5 秒后回读，失败保留本地列表并提示。
- 系统 `wpa_supplicant` 已保存网络先由系统自动连接（约 15 秒）；失败后按本列表
  逐个尝试（每个最多 30 秒），整轮失败后每 15 秒重试。

### 方式一：U 盘 / TF 卡 `test.cfg`

FAT32 U 盘（或 TF 卡）根目录放 `test.cfg`：

```json
{ "ssid": "your-wifi-ssid", "pwd": "your-wifi-password", "isConnect": true }
```

插入设备后重新上电；U 盘对应 `/mnt/usb1/test.cfg`，TF 卡 `/mnt/extsd/test.cfg`。

### 方式二：adb push `/data/multitimer.wifi`

```powershell
.\tools\platform-tools\adb.exe connect <设备IP>
.\tools\platform-tools\adb.exe push .\multitimer.wifi /data/multitimer.wifi
.\tools\platform-tools\adb.exe shell reboot
```

> WiFi 密码以明文保存在 `/data`；仓库和文档不记录真实密码。设备已能打开手机页时，
> 也可在「网络与对时」卡片里维护列表，不必手工 push。

---

## 使用

1. 设备上电；首次连上 WiFi 后 LED 显示约 12 秒 IP（全屏滚动，无前缀；错过可中键单击）。
2. 手机连同一局域网，浏览器打开 `http://<设备IP>:8080/`（或 `http://<设备IP>/`，
   80 会 302）；页面只用当前 origin（自动补 `:8080`），不需要也不保存 IP 地址列表。
3. 在页面里：
   - 查看设备 IP/SSID/运行时长/本地时间/时区/对时状态（IP 由 DHCP 自动获取）；
   - 维护最多 8 个 WiFi 网络（增删/启停/排序/编辑），保存并重连；填自定义 NTP；
   - 选择时区（UTC-12..UTC+14 含半小时/45 分钟）或分钟偏移（-720..840）；
   - 设置时钟/倒计时/**正计时**/预警颜色与预警秒数；
   - 维护最多 12 个目标（日期 + `HH:MM:SS`，可上移/下移/删除/移除已过期；
     备注只存 localStorage）；
   - **编辑页面**：新增/删除/排序页面，选择 5 种类型（含“日期 + 时间”）；live 页填本页默认指标与
     2 个直播间（平台/房间号/label/启用/指标覆盖）；设置全局直播数据源与刷新参数；
     需要看每个房间的状态/年龄/错误时调用 `GET /api/live/status`；
   - 用 52×16 模拟器预览任意页面（含分屏、直播两行、**正计时**与 **datetime 日期+时间**），
     可手动时间与倍速播放；
   - 点 **保存到时钟（热生效）**：设备校验、写入 `/data/multitimer.conf` 并应用；
     成功后页面重新读取配置并静默对时一次。
4. 页面加载时若设备时间无效或与浏览器差 >2 秒，会提示并自动对时；设备联网后
   每分钟 SNTP 校时（自定义服务器优先/回退内置）。
5. 完全没网、也不知道设备 IP 时：**右键长按 3 秒**进入 SoftAP，手机连
   `TC002-Setup`，访问 `http://192.168.100.1/` 配网；保存列表后设备退出 AP 重连。
6. 配置完成后手机可关掉页面，设备继续独立显示/倒计时/轮询直播。
7. 目标到达后的显示：有未来目标时倒计时；没有未来目标但有已过期目标时**正计时**
   （从最近过期目标累计，`colors.countup`）；完全没有有效目标时显示 `NO COUNTDOWN`
   （dim）。需要看日期时添加一个 `datetime` 页。

> 目标是一次性的，到达后过期，不会每天循环。设备没有时区数据库、不自动 DST：
> 本地时间 = UTC + 分钟偏移。直播页数据：B 站由设备直连 HTTPS，抖音/快手经 bridge；
> 设备断网时会保留最后成功值并变暗（陈旧），不会崩。

---

## 故障排查

### 页面 / 旋钮

- **旋钮不翻页**：确认页面数 ≥2 且至少 2 个启用页；只有 1 个启用页时旋钮无动作
  （设计如此）。旋钮事件有 120ms 去重，快速连转会连续翻页。
- **翻页没有横幅**：横幅只在页面真正改变时出现 1200ms；只有 1 页不出现；
  覆盖层（IP/AP 提示）激活时横幅被抑制。
- **重启后没有回到最后停留页**：`uiState` 有 30 秒稳定 + 120 秒最小间隔节流，
  掉电前最近的切换可能未落盘；属预期。可在网页“设为此页”或调用
  `POST /api/page` 立即切页；`{"reset":true}` 清记忆后回 `defaultPageId`。
- **`clock_countdown` 下半空白**：时间无效时下半固定留空；时间有效时不应空白
  （有未来目标→倒计时、有已过期目标→正计时、完全没有有效目标→`NO COUNTDOWN`）。
  旧配置的 `display.splitBottomWhenIdle=blank` 已被忽略，改不改都不影响屏幕。
- **`countdown` 页显示 `NO COUNTDOWN`**：说明当前没有任何有效目标（v3.3 起不再回时钟，
  也不再显示日期）；添加/启用未来目标或让目标过期即可看到倒计时/正计时。
- **没有日期显示**：v3.3 起日期由单独的 `datetime` 页展示；需要日期/星期请添加该页。
- **目标到点后没有回到时钟**：v3.3 预期行为，到点后进入**正计时**
  （从最近过期目标累计，`mode=countup`），不再自动回时钟。

### 直播

- **直播页显示 `--`**：分情况——
  `live.source=off` → 不取数（设计如此）；
  `source=bridge/hybrid` 但 `live.url` 为空 → 抖音/快手 `--`；
  bridge 不可达/未启动 → 房间失败，屏幕 `--` 或保留旧值变暗；
  房间号为空/禁用 → 该行留黑（不显示 `--`，表示未配置）。
- **整页 `NO LIVE`**：本页没有任何“已启用且填了房间号”的槽位；
  在网页页面编辑里填房间号或启用槽位后保存。
- **B 站显示 `--`**：v3.3 已支持设备 HTTPS 直连（镜像 `/res/lib` 带 OpenSSL 1.1.1w）。
  先确认镜像已含 `/res/lib/libssl.so.1.1` + `libcrypto.so.1.1`、设备时间有效
  （TLS 证书依赖时间）、`/res/ui/cacert.pem` 存在；失败时 `hybrid` 会回退 bridge
  （`live.url` 非空）并 60 分钟退避。若 `live.source=off` 或房间号为空则不会有数据。
- **抖音/快手一直 `--`**：先确认 bridge 在 PC/NAS 运行且电脑与设备同网段：
  `curl.exe "http://<PC-IP>:8081/health"`；再确认 `live.url` 端口/路径正确且为
  `http://`；抖音被限流时 bridge 返回 `rate_limited`，等 3–30 分钟再试；
  快手需要 Playwright（否则 `kuaishou_requires_browser`）。
- **数据不刷新**：轮询周期 `live.refreshSec`（默认 30s）；切页/旋钮按键/保存配置会
  立即刷新。`GET /api/live/status` 可看到每个房间的状态、年龄与错误。
- **数值 0 不是失败**：未开播房间 `online:0`、`likes:0` 是有效值；只有
  `ok:false`/字段缺失才是 `--`。
- **bridge 安全**：协议明文无鉴权，只放可信局域网，不要公网映射；
  不要给设备配 `https://` 的 bridge 地址（只接受 `http://`）。

### 网页

- **打开页面提示“设备仍是 v2 固件/配置版本不支持”**：设备还是 v2 固件或旧页面；
  刷入 v3 固件并强制刷新（Ctrl+F5）网页。
- **用 `file://` 打开网页**：无法自动确定设备地址，会提示“请通过设备 IP 打开页面”；
  请用 `http://<设备IP>:8080/` 打开（IP 由设备 DHCP 获取，中键单击可看）。
- **保存报 400**：页面会显示具体字段错误（`tzOffsetMinutes`/颜色/目标/页面/房间/
  `live.url` 等）；按提示修正。v2 旧数据不能直接 POST 给 v3 固件，
  刷新页面重新加载配置后再保存。
- **WiFi 密码**：输入框不回显、不写浏览器存储；同 SSID 留空 = 保留原密码；
  新 SSID 留空 = 开放网络。
- **XML/HTML 注入**：直播间标题、SSID、错误文本等第三方内容只按纯文本显示。

### LED 不显示 / 一直 `--:--:--`

- `--:--:--` 表示**时间未同步**（系统时间早于 2023-11-14），不是“没有目标”；
- 先联网让 SNTP 成功，或在手机页点“立即对时”；
- 看 logcat 是否有 MCU 版本日志（每次开机必须先查询 MCU 版本，否则 LED 不工作）；
- 确认 `sys.zkapp.state=running` 防砖设置已执行；检查设备是否在运行旧 `/tmp` 程序。

### 时间不对

- **首次开机完全离线**：无 RTC，只能 `--:--:--` 或恢复最后保存时刻（可能落后）；
- **重启后慢几十秒**：每 300 秒才落盘一次，恢复的是上个保存点；
- **时区不对**：填分钟偏移（中国 `480`、印度 `330`、尼泊尔 `345`），不要填小时；
- **不自动校时**：确认 WiFi 已连、路由器未封 UDP 123；自定义 `ntpServer` 失败会
  回退内置；被封锁时只能用网页对时；
- **没有 DST/时区库**：跨夏令时手动调整分钟偏移。

### 离线 / 断网

- SNTP/网页对时成功后断网继续走时；倒计时照常；
- 重启且完全离线只能恢复到上次保存时刻（断电期间未知），联网后校正；
- 直播断网保留最后值并变暗（陈旧），恢复后自动刷新；不阻塞计时。

### 时区与目标

- 目标按“当前时区本地墙上时间”解释，改时区后目标 UTC 时刻与剩余秒数立即变化；
- 目标日期 2000-01-01..2099-12-31，时间必须带秒；
- 过期目标不自动删除（网页可“移除已过期”）；v1 旧时间点只迁移为当天一次性目标。

### 连不上 WiFi / 页面打不开

- 按中键：`NO WIFI` 表示没连上；`IP` 表示已有地址；`AP ...` 表示在配网热点；
- 确认手机与设备同一局域网，路由器未开 AP 隔离/访客隔离；优先 2.4 GHz；
- 浏览器访问 `http://<IP>:8080/` 或 `http://<IP>/`，不要用 `https://`；
- 电脑上自测：`curl.exe http://<设备IP>:8080/api/config`；
- logcat 若出现 `http server failed to bind`，8080 被占用（LED 短暂 `HTTP ERR`）；
  只有 `port 80 redirect unavailable` 时 8080 仍可用，手动带 `:8080`；
- 设备侧 HTTP 无鉴权，部分企业/公共 WiFi 会隔离客户端互访。

### WiFi 列表不生效 / 保存后不回连

- 列表最多 8 个；重复 SSID/超限/类型错返回 400（网页显示错误）；
- 同 SSID 空密码保留原密码；新 SSID 空密码 = 开放网络；保存是整表覆盖；
- 保存后约 800ms 断开重连；网页约 2.5 秒回读，重连期失败属正常；
- 系统自动连接（约 15 秒）优先，失败后才按列表逐个尝试（每个 30 秒）；
- 保存空列表 = 显式没有网络，不再回退；删除 `/data/multitimer.wifi` 才回退。

### SoftAP 配网问题

- **右键长按 3 秒**没反应：确认按右键、时长足够；看 logcat 是否有
  `mainLogic: right long press detected` / `ConfigMode: enabling SoftAP`；
  **物理右键长按的 LED 观感仍需用户现场确认**（v2 时代入口是中键长按）；
- 能看到 `TC002-Setup` 但连不上：确认密码为固件固定值 `12345678`、AP 为 2.4 GHz；
  AP 与 STA 互斥，进入 AP 后原局域网断开；
- 连上 AP 打不开页面：先试 `http://192.168.100.1:8080/`；80 绑定失败时需手动 `:8080`；
- AP 15 分钟无 API 活动自动关闭；保存列表会先退出 AP 再重连；
- AP 密码是固件固定值 `12345678`，配网完成后建议右键长按退出。

### 复位后配置全丢

- 长按复位键恢复官方固件会**清空 `/data`**（配置、WiFi、时钟状态全部丢失），
  需重新配网、保存配置、对时；
- `/data` 没有完整可刷回镜像；复位前确认接受该最坏情况。

### 如何看 logcat

```powershell
.\tools\platform-tools\adb.exe connect <设备IP>
.\tools\platform-tools\adb.exe logcat -c
.\tools\platform-tools\adb.exe logcat -v time
# 只看关键日志：
.\tools\platform-tools\adb.exe logcat -v time | findstr /i "zkgui MultiTimer PageHost LiveService WifiBootstrap HttpServer TimeKeeper Bilibili mcuVer FATAL"
```

常见关注点：

- `TimeKeeper: restored system clock from flash`：冷启动恢复时钟；
- `WifiBootstrap: SNTP sync ok` / `SNTP check ok`：SNTP 成功（INFO/DEBUG）；
- `mainLogic: loaded /data/multitimer.conf`：配置加载；
- `PageHost: config applied pages=N` / `page -> N` / `persisted uiState`：页面系统；
- `LiveService: config source=... refreshSec=...`：直播配置生效；
  `BilibiliClient: room ... failed`：B 站直连失败原因（v3.3 一般应成功；失败时看 TLS/网络）；
- `http server: port 80 -> 8080 redirect enabled` / `unavailable`：80 跳转；
- `http server failed to bind`：8080 绑定失败；
- `FATAL EXCEPTION` / `tombstone`：应用崩溃，记录完整堆栈。

---

## 真机验证状态

### 已在真机完成（v2.1.2 历史基线，2026-10-22）

- [x] v2.1.2 启动、防砖标志、MCU 初始化/版本查询、52×16 SPI 刷新无报错；
- [x] FlyThings 构建 `0 errors, 0 warnings`（19:05:26）；
- [x] 主机单测 102/102；网页自检 62/62（v2.1.2 时期）；
- [x] `/tmp` 部署与 `/res` 固化两种方式均能启动；
- [x] v2.1.2 已固化 `/res`：`/tmp` 为空、`/res/lib/libzkgui.so` 480,876 字节、
      回读 SHA256 与 `dist` 一致、`/data` 保留、WiFi 仍连接；
- [x] `GET /api/status` 200、`GET /api/wifi` 200、`GET /api/config` 200；
      80 端口 302 到 `:8080`；
- [x] v2 回归项（双目标顺序切换、预警、时钟回拨/改时区即时重算、
      `4d04:02:02` 小号字、配置重启保持、冷启动恢复+SNTP 校正）；
- [x] 网页多地址、WiFi 列表（≤8、空列表、2.5 秒回读）、AP IP `192.168.100.1`、
      80→8080 跳转（开发期实测）。

### v3.3 真机验证（2026-10-22 已完成）

- [x] 镜像生成与固化：`dist/update.img` 1,942,076 字节 / `8B2BCF20...`（21:49:28 生成），
      官方 ADB 刷入 `/res`；`/tmp` 为空、`/data` 保留、回读
      `/res/lib/libzkgui.so` = 722,532 字节 / `1A495991...`（与 `dist` 一致）；
- [x] 镜像内含 OpenSSL 1.1.1w（`/res/lib/libssl.so.1.1` + `libcrypto.so.1.1`）；
- [x] **NO COUNTDOWN**：无目标时 p1 `text2=NO COUNTDOWN`、p2 `text=NO COUNTDOWN`；
- [x] **datetime 页**：新增 p4 datetime，实测 `text=22:01:27`、`text2=2026-10-22`；
- [x] **B 站设备直连**：停止 bridge 后，B 站示例房间（测试用）仍 `state=ok`；
- [x] 页面系统：p0 clock / p1 8+8 分屏（同一 now）/ p2 countdown / p3 live /
      p4 datetime 正常；
- [x] 配置迁移保留；`colors.countup` 缺省补 `#00BFFF`；
- [x] 网页 v3.3：Edge 自动地址加载（无 IP 输入框）、含正计时颜色、
      页面类型下拉含“日期 + 时间”；
- [x] `/api/status`（含 `countup`/`overlay`）、`/api/config`、`/api/page`、
      `/api/live/status` 正常；80→8080 跳转正常。

### v3.3 仍待用户确认（不要当成已验证）

- [ ] **物理中键单击显示 IP、右键长按 3s 进入/退出 SoftAP 的 LED 观感**
      （IP 滚动速度、AP SSID+密码两行、AP OFF/AP ERR）与按键手感；
- [ ] 8 个 WiFi 网络逐个失败/成功切换与重连耗时；
- [ ] 自定义 NTP 真机校时/失败回退、每分钟 NTP 长日志、多时钟地址；
- [ ] 预警色/远目标 `>=100h` 小号字/时间无效 `--:--:--` 的目视确认；
- [ ] 网页真机保存热生效、390px 布局；
- [ ] 24h+ 内存/线程/HTTP 稳定性；复位键恢复官方固件演练；
- [ ] B 站直连长期稳定性（接口/证书/时间/弱网）。

### v3.3 已知差异与注意事项

- `display.splitBottomWhenIdle` 已废弃：网页 UI 已移除，固件渲染忽略 `blank`，
  无目标固定显示 `NO COUNTDOWN`；旧网页/脚本读写该字段仍可兼容。
- **网页直播数据源文案已更新**：下拉/提示为“B 站设备直连 + 抖音/快手中继”，
  与 v3.3 固件一致（`v3g` 起）；以本文与真机行为为准。
- **QA 报告滞后**：[`../../docs/QA-v3-initial.md`](../../docs/QA-v3-initial.md) /
  [`../../docs/QA-v3-final.md`](../../docs/QA-v3-final.md) 是 v3.0 基线；
  v3.3 暂无独立 QA 报告（由 Lead 实现并真机验证）。
- `/api/live/status` 实际字段为 `source{kind,url,reachable,lastAttemptUtc,
  lastOkUtc,error}` + `rooms[]`，比契约简表更全；以实际响应为准。
- B 站直连失败后 60 分钟退避（`live.url` 非空时回退 bridge）；抖音/快手仍依赖 bridge
  （明文 + 无鉴权，只可信局域网）。
- 固件与网页模拟器对“空直播槽”占位不同：固件“启用但房间号为空”留黑、整页无已配置
  房间才 `NO LIVE`；网页模拟器会显示 `label + --`。**以固件为准**。
- 旋钮有 120ms 软件消抖；物理按键/IP 滚动观感待用户确认。
- **datetime 页时间字距自适应**：先 spacing=2，超 52px 降 1/0；时间无效显示 `--:--:--`。
- 设计文档 `DESIGN-pages-v3.md`/`DESIGN-web-v3.md` 中“一页 4 个/2×2”、旧按键/旧地址
  等为 v3.0 旧稿，以实现与本文为准。

构建与接口细节见 [`docs/HANDOFF-native.md`](../../docs/HANDOFF-native.md)；
当前快照见 [`docs/STATE-2026-10-22-v3.md`](../../docs/STATE-2026-10-22-v3.md)；
固化流程见 [`docs/FLASH-RUNBOOK.md`](../../docs/FLASH-RUNBOOK.md)；
发布包与 hash 见 [`dist/README.md`](../../dist/README.md)。










