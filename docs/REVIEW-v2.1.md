# TC002 MultiTimer v2.1 回归 / 代码审查

> 审查方式：只读审查（读源码、Git diff、构建产物、真机日志/HTTP）。
> **未修改任何代码、未执行 git commit、未改动设备配置。**
> 审查快照：2026-10-22 19:00–19:10（Asia/Shanghai）；v2.1.2 于 19:05 构建、19:06 刷入，docs 于 19:09 仍在更新。
> 审查期间工作区仍在被并行修改；本文的行号与结论以文末「快照哈希」为准。若源码再变，请按哈希复核。

---

## 0. 结论摘要

1. **时钟/倒计时严格绑定仍成立**：`TimerPage::gatherFrame()` 每秒调用 `time(NULL)` 后重新 `TimerEngine::evaluate()`，没有剩余秒数递减缓存。`POST /api/time`、改时区、SNTP 跳时都会在下一秒反映。
2. **多 WiFi 列表主路径基本正确**：GET 不回传密码；同 SSID 空密码保留；重复 SSID/超过 8 个返回 400；保存走 `tmp + fsync + rename`；v1 单对象可读。旧 `POST /api/wifi` 的空密码清空问题已在 18:58 修复。
3. **SoftAP 配网仍未完成验收**。代码已有生命周期、15 分钟超时、HTTP 活动续期、退出请求、AP 期间暂停 STA 等机制；v2.1.2 起 LED 会显示两行 `TC002-Setup` / `12345678` 10 秒，但**从未有一次“按住中键 ≥3 秒”的成功日志**，热点是否真能起来仍未验证。
4. **18:51 / 11:02 两次 long-press 测试都是无效测试**：日志显示 down 后 18–22 ms 就收到 up，UI 的 1 s tick 不可能看到 3 s 持续按住。`apMode=false` 是预期结果，不能据此判定 `ConfigMode` 或 KeyManager 有 bug。详见 §5。
5. **没有发现必然变砖/丢数据的代码级 P0**。P0 只剩 **AP 长按路径未验证**（P0-2）；19:06 复查时 19:30/20:00/20:30 三个目标已保存，原 P0-1 已解除。
6. **当前镜像已刷入 `/res` 并正常启动**：`/res/lib/libzkgui.so` = `7A762407…`（480,876 B，v2.1.2），`/tmp` 为空，`/data` 保留，`GET /api/status` 200（`mode=countdown`），端口 80 → 8080 返回 302。**本次刷机本身没有变砖，但 v2.1 不应据此宣布完成。**
7. **建议：19:30 前先保存三个目标并确认**；在 P1 修完、长按真按住验证通过之前，**不要再刷下一版镜像**。

---

## 1. 审查快照与证据

### 1.1 工作区源码（`git diff` + 未跟踪新文件）

| 文件 | SHA256 前缀 | 备注 |
|---|---|---|
| `src/net/HttpServer.cpp` | `C05DB7517E38` | 多 WiFi API、端口 80、AP 状态字段、`touch()`、AP 退出 |
| `src/net/ConfigMode.cpp`（新） | `4A088032A092C1F7` | SoftAP 状态机；v2.1.2 修了 `exit()` 的 `mActive` 顺序 |
| `src/net/ConfigMode.h`（新） | — | `enter/exit/requestExit/touch/tick` |
| `src/net/WifiManager_task.cpp` | `B3D7407942F08AB4` | 多网络顺序连接、60 s SNTP、AP 暂停；v2.1.2 加了连接循环 AP 检查 |
| `src/logic/mainLogic.cc` | `D0D2269A3BCC0696` | 长按 3 s、AP 覆盖层（v2.1.2 起显示 SSID+密码两行 10 s）、`SntpClient::setServer` |
| `src/pages/TimerPage.cpp` | `41FD7F852D9AC8DC` | v2.1.2 起 `renderOverlay` 先按 `\n` 拆行，再按空格 |
| `src/config/WifiStore.cpp` | `CFB4B05355AA` | 列表解析/保存/原子写 |
| `src/config/WifiStore.h` | `C7954BDFB84E` | `WifiNetwork`、`kMaxNetworks=8` |
| `src/time/TimeKeeper.cpp` | `A7730D687D43` | `noteSynced` 延迟落盘 |
| `src/net/SntpClient.cpp` | `1B6BEE1DDEC5` | 自定义 NTP 服务器 |
| `src/config/ConfigStore.cpp` | `0B0CB1514A2A` | `ntpServer` 字段 |
| `src/timer/TimerTypes.h` | `9CAF2A653F85` | `ntpServer` |
| `web-src/index.html` | 97,477 B | WiFi 列表、NTP 输入 |
| `src/web/editor_gz.h` | 155,525 B，`EDITOR_GZ_LEN=25352` | 与 web-src gzip 后 25,352 B 一致 |

> 源码在 19:05 又更新到 **v2.1.2**（`ConfigMode.cpp` 的 `exit()` 顺序、`connectWithSavedCredential()` 的 AP 检查，共 +8 行）；本文行号以 19:07 快照为准，仍以哈希复核。

### 1.2 构建 / 镜像 / 设备

| 项目 | 值 |
|---|---|
| 当前源码构建 `build/v212-workspace/.../libzkgui.so` | 480,876 B，SHA256 `7A76240720B558CA…`，19:05:26（v2.1.2） |
| `dist/lib/libzkgui.so` | 480,876 B，SHA256 `7A76240720B558CA…`（19:05:26） |
| `dist/update.img` | 762,428 B，SHA256 `A3C1655AD16C27A18D6D25DAC1835D265EB8256CD7E802EABFB99923132FE9B8`（19:05:56） |
| 设备 `/res/lib/libzkgui.so` | 480,876 B，SHA256 `7A76240720B558CA…`（19:06 本地 pull 回读确认） |
| 设备 `/tmp` | 空（已从 `/res` 启动） |
| 设备 `/data` | `multitimer.conf`、`multitimer.wifi` 保留；**19:06 已保存 19:30/20:00/20:30 三个目标** |
| 主机单测 | **102/102**（`tests/run_tests.ps1`，本次实跑） |
| 网页自检 | **62/62**（`node native/MultiTimer/tools/check_web.js`，本次实跑） |
| 真机 HTTP | `/api/status` 200（`mode=countdown`、`remainSeconds≈1406`）；`/api/wifi` 200；`/`（80）302；`/api/config` 200 |

### 1.3 真机日志证据（关键）

设备时间比本地时间早 8 h（UTC）：

```text
10-22 10:51:35.155 I/zkgui: [mainLogic.cc:122] mainLogic: middle key status=1
10-22 10:51:35.173 I/zkgui: [mainLogic.cc:122] mainLogic: middle key status=0
```

刷机后再次测试：

```text
10-22 11:02:34.289 I/zkgui: [mainLogic.cc:124] mainLogic: middle key status=1
10-22 11:02:34.311 I/zkgui: [mainLogic.cc:124] mainLogic: middle key status=0
```

两次都只间隔 **18 ms / 22 ms**，且没有 `middle long press detected` / `SoftAP config mode on` / `ConfigMode: enabling SoftAP` 日志。

NTP（`/tmp` 旧构建时期）60 s 节流实测：

```text
10-22 10:51:49.753 WifiBootstrap: SNTP sync start (system time=1792666309)
10-22 10:51:49.840 TimeKeeper: noteSynced epoch=1792666309 (persist deferred)
10-22 10:52:49.756 WifiBootstrap: SNTP sync start (system time=1792666369)
10-22 10:52:49.842 TimeKeeper: noteSynced epoch=1792666369 (persist deferred)
```

端口 80 302 实测（STA 网络）：`HTTP 302`，`Location: http://192.168.31.124:8080/`。

---

## 2. P0（阻断 19:30 验收 / 阻断 v2.1 结论）

> 说明：**没有发现代码级 P0**（无必然变砖、无无条件丢数据、无启动即挂）。以下两项是验收/验证门禁，必须先行处理。

### P0-1（19:06 已解除）设备目标为空

- 19:02 现场：`GET /api/config` 返回 `"targets": []`，`/api/status` 为 `mode=clock`、`targetUtc=0`。
- **19:06 复查已保存**：`targets` = `2026-10-22 19:30:00 / 20:00:00 / 20:30:00`；`/api/status` 变为 `mode=countdown`、`remainSeconds≈1406`、`targetLocal="2026-10-22 19:30:00"`。**此 P0 解除。**
- 仍需注意：`README.md:131` 还写着“当前设备 targets 为空”，已过时；19:30 前不要再清空/覆盖配置。若重新刷机，确认 `/data` 保留并用上面两条命令复核。

### P0-2 AP 长按路径从未成功验证；当前注入方法必然失败

- 已有两组日志证明按下后 18–22 ms 就收到释放；`mainLogic.cc:239-241` 要求 `>=3000 ms`，UI tick 1 s 一次，不可能命中。
- v2.1.2（19:06 刷入）尚未做长按测试；当前 `logcat` 没有 `ConfigMode: enabling SoftAP`。
- 影响：**不能把“AP 不工作”当成代码 bug 去改**；也**不能在未验证的情况下宣布 v2.1 完成**。
- 处理：按 §5 的“保持按住”方法重测；通过后再做 AP/STA/端口 80 的完整验收。

---

## 3. P1（下一版镜像 / 宣布 v2.1 前必须修）

### P1-1 固定 AP 弱口令（跨设备同口令）

- `src/net/ConfigMode.cpp:16`：`kApPassword = "12345678"`；`:65-67` 设置 SSID/密码并开启 AP。
- **可发现性 v2.1.2 已改善**：`mainLogic.cc:255-257` 用 `SSID + "\n" + password` 显示两行 10 s；`TimerPage.cpp:199-206` 先按 `\n` 拆行。README:54-56、native README:68-70 也写了同样内容。
- 仍存在的问题：所有设备共用 `TC002-Setup` / `12345678`；15 分钟窗口内，同场地知道默认值的人可连上热点，再通过无鉴权的 HTTP 改 WiFi/配置（P1-2）。
- 建议：按设备序列号/MAC 派生每台不同密码，或用一次性随机密码显示在 LED；保存 WiFi 后立即退出 AP，缩短暴露窗口。

### P1-2 无鉴权 + `Access-Control-Allow-Origin: *`，存在跨站改写风险

- `src/net/HttpServer.cpp:212`、`:236`：所有 JSON/204 响应带 `Access-Control-Allow-Origin: *`；
- `:816-839`：所有 API 无 token / Origin / Host / Content-Type 校验；
- `:696-740`：`parseRequest` 只解析，不拒绝 `text/plain` 等简单请求体；
- 可达的写接口：`POST /api/config`（`:823`）、`POST /api/time`（`:825`）、`POST /api/wifi/list`（`:829`）、`POST /api/wifi`（`:831`）。
- 影响：用户浏览器访问任意网页时，该网页可向局域网内设备发简单 POST（无需预检）：
  - `POST /api/wifi/list {"networks":[]}` 清空 WiFi 列表；
  - 改时区/目标/颜色、改时钟；
  - 读取 `GET /api/wifi`（SSID 列表、是否有密码）与 `/api/status`。
  端口 80→8080 的 302 也让设备更容易被网页探测到。
- 建议（任一/组合）：写接口要求自定义头（触发 preflight）+ 设备 token；校验 `Origin`/`Host`；去掉 `*`；要求 `Content-Type: application/json`；只监听局域网接口；对 `/api/wifi*` 与 `/api/config` 增加鉴权。

### P1-3 AP/STA 协同剩余竞态：reconnect 跨线程 + 双重重连

> v2.1.2 已修主要两项（连接循环 AP 检查、`exit()` 顺序），下面剩两项属于稳定性风险，不再解释“AP 为什么打不开”。

当前已做（v2.1.2）：`WifiBootstrap::run()` 在 AP 激活时暂停（`WifiManager_task.cpp:318-322`）；保存 WiFi / `reconnect()` 时请求关闭 AP（`HttpServer.cpp:445-448`、`WifiManager_task.cpp:126-131`）；`connectWithSavedCredential()` 每轮检查 AP（`:210-213`）；`ConfigMode::exit()` 先 `setEnable(false)` 再置 `mActive=false`（`ConfigMode.cpp:72-90`）。

仍存在的问题：

1. ~~`connectWithSavedCredential()` 循环内没有检查 AP 状态~~：**v2.1.2 已修**（`WifiManager_task.cpp:210-213`）。
2. **`reconnect()` 从 HTTP 派生的 detached 线程调用 `wm->disconnect()`**（`HttpServer.cpp:425-431` → `WifiManager_task.cpp:121-137`），而 bootstrap 线程同时在调用 `wm->connect()/isConnected()`。厂商 `WifiManager` 是否线程安全没有任何保证；这是最可能造成偶发卡死/崩溃的并发点。
3. ~~`isActive` 在 `setEnable(false)` 之前就变 false~~：**v2.1.2 已修**（`ConfigMode.cpp:72-90`，注释已说明）。
4. **`mReconnectRequested` 在 AP 暂停期间保持 true**：AP 退出后第一次连上，内层循环会立即消费该标志，额外做一次 disconnect/reconnect（多一次 5–20 s 抖动）。

建议：把所有 `WifiManager` 调用收敛到 bootstrap 单线程（HTTP 只投递命令）；AP 退出时清 `mReconnectRequested`。这两项属于长稳/并发风险，建议在 24 h 压测前修。

### P1-4 空列表 / 全禁用 / GET 失败可把设备留在“无凭据”状态

- `POST /api/wifi/list` 接受 `{"networks":[]}`（`HttpServer.cpp:496` 只拒绝 >8），保存成功后重连（`:433-451`）。
- `WifiStore::loadNetworks()` 把显式空列表视为合法且**不再回退**到系统 `wpa_supplicant.conf`/`setting.ini`/`test.cfg`（`WifiStore.cpp:351-357`）。
- `WifiBootstrap::connectWithSavedCredential()` 对空列表/全 `enabled:false` 不尝试任何网络（`WifiManager_task.cpp:193-196`、`:207-209`）。
- `GET /api/wifi` 在 `loadNetworks()` 返回 false 时仍回 `ok:true + networks:[] + source:""`（`HttpServer.cpp:453-480`）；网页会把“读取失败”当成“空列表”，用户可能顺手保存空列表（网页空态文案见 `web-src/index.html:1070`，自检也明确允许保存空列表：`check_web.js` case 57）。
- 影响：若系统 `wpa_supplicant` 里也没有可用网络，设备会一直离线；恢复只能靠 AP 配网（密码只在文档里，见 P1-1）或 USB/复位。即使系统有网络，`enabled:false` 也**阻止不了系统自动连接**（`run()` 先等系统自动连接 15 s），UI 的“停用/顺序”语义被高估。
- 建议：空列表要求显式 `"confirmClear":true` 或二次确认；`GET /api/wifi` 在读取失败时返回 `ok:false` 或 `source:""` 标识；文档说明“停用只影响应用主动连接，不影响系统自动连接”；验证 AP 恢复路径。

### P1-5 文档自相矛盾 + v2.1 快照缺失（发布质量）

`README.md` 与 `native/MultiTimer/README.md` 的 v2.1 段落已经补齐（多 WiFi、AP、80→8080、NTP 都有），`docs/STATE-2026-10-22-v21.md` 也已在 19:08 创建；但该快照写的是 v2.1.1（`A9AE6B05`/`targets` 为空），而设备在 19:06 已刷 v2.1.2（`7A762407`）且已保存 3 个目标；`HANDOFF-native.md`、`FLASH-RUNBOOK.md` 仍是旧 v2 数据，`tests/README.md` 的源码快照哈希仍是旧的。具体见 §7。这会让现场按错误的“最近对时 6 小时/92 用例/旧镜像哈希”操作。

---

## 4. P2（建议修，不阻断本次刷机）

| ID | 位置 | 问题 | 建议 |
|---|---|---|---|
| P2-1 | `ConfigMode.cpp:65-69` | `enter()` 在 AP 真正 ready 前就返回 true 并置 `mActive=true`；1 s 后 `tick()` 可能因 `E_SOFTAP_ENABLE_ERROR` 退出。UI 先显示 `AP …`，`/api/status` 先报 `apMode=true` | 让 `enter()` 只发起，`apMode` 以 `E_SOFTAP_ENABLED` 为准；UI 用“AP 启动中” |
| P2-2 | `ConfigMode.cpp:124-129` | `tick()` 只处理 `E_SOFTAP_ENABLE_ERROR`；若状态卡在 `DISABLED/ENABLING`，`mActive` 会保持 true 直到 15 min 超时 | 增加 DISABLED 判定/启动超时（如 30 s） |
| P2-3 | `ConfigMode.cpp:111-115` | `requestExit()` 只由 UI 线程的 `tick()` 消费；UI 卡住时 AP 关不掉 | 后台线程也能安全退出，或用条件变量/事件队列 |
| P2-4 | `mainLogic.cc:238-265` | 长按用 1 s UI tick：实际阈值 3–4 s；3.2 s 松手会在 release 时走短按显示 IP；UI 被 flash/AP 阻塞时会漏检 | 用 100 ms 定时器或把 hold 检测放进 KeyManager；release 时也按持续时间判定 |
| P2-5 | `HttpServer.cpp:140` | 端口 80 `getsockname` 失败时回退 `192.168.4.1`，但本机 AP 实测 IP 是 `192.168.100.1`（README:55、native README:69），回退值错误 | 回退用 `ConfigMode::ip()`，再退到 `192.168.100.1` |
| P2-6 | `HttpServer.cpp:175-198`、`:901-905` | 端口 80 在开机时无条件监听；bind 失败只打 warning，AP 启动时不重试；`redirectLoop` 每连接一个 detached 线程、无并发上限 | 进入 AP 时按需启动/重试；限制 redirect 线程数；失败时网页/文档给出手动 `:8080` |
| P2-7 | `HttpServer.cpp:453-480` | `GET /api/wifi` 不检查 `loadNetworks` 失败，返回空列表 | 区分 `ok:false` / `source:""` |
| P2-8 | `HttpServer.cpp:501-571` | 读已有列表→合并密码→写回，非原子；两个并发 POST 会 last-writer-wins，可能丢密码/顺序 | 在 `WifiStore` 内提供带锁的 merge/save，或整体串行化写 |
| P2-9 | `WifiStore.cpp:304-306`、`:316-320` | 主文件里 `networks` 超过 8 项时 `parseJsonNetworks` 返回 false，`loadNetworks` 转而回退到系统凭据（可能连到别的网络） | 超限时截断/报错并保留主文件，不要静默回退 |
| P2-10 | `WifiStore.cpp:193-215` | `fsync(file)` + `rename`，但未 `fsync` 目录；极端掉电可能丢 rename | 打开 `/data` 目录 `fsync`；或接受并在文档说明 |
| P2-11 | `WifiManager_task.cpp:195-231` | 8 个网络顺序尝试，每个等 30 s，最坏 4 分钟；网页提示“约 800 ms 后重连/短暂离线”过于乐观 | 文档写清最坏时间；或缩短单网络等待/并行探测 |
| P2-12 | `WifiManager_task.cpp:348` | 注释仍写“内部另有 6 小时成功节流”，代码是 60 s | 同步注释/文档 |
| P2-13 | `WifiManager_task.cpp:236-239`、`:258-260` | 从未成功对时（`last=0`）时每 30 s 尝试一次；失败每 30 s 一条 `LOGW`；成功后每 60 s 一条 `LOGD` | 失败日志限频（如每 10 min）；未对时也按 60 s 节流 |
| P2-14 | `SntpClient.cpp:143-167` | `getaddrinfo` 无超时；若 DNS 卡住，`mTimeSyncInFlight` 永远不释放，后台 SNTP 停止 | 用带超时的解析（或线程+超时）并保证 flag 复位 |
| P2-15 | `SntpClient.cpp:169-188` | 每 60 s 只要偏差 >1 s 就 `settimeofday`；可能让显示秒回退/重复一次 | 用 `adjtime`/渐进校正，或只在偏差 >2 s 时跳变并接受文档说明 |
| P2-16 | `TimeKeeper.cpp:186-201`、`:208-227` | `tick()` 在 UI 线程持锁写 flash（每 300 s）；HTTP 的 `TimeKeeper::now()` 可能被阻塞 | 落盘移到后台线程/无锁快照；保持 300 s 节流 |
| P2-17 | `TimeKeeper.h:25`、`DESIGN-v2.md:33`、`HANDOFF-native.md:211`、`native/README.md:102`、`README.md:98` | 文档/注释仍写 `noteSynced` 立即持久化；代码已延迟到 `tick()` | 统一改为“首次有效立即 saveState，之后随 300 s tick 落盘” |
| P2-18 | `TimeKeeper.h:12`、`HttpServer.cpp:403`、`SntpClient.cpp:188` | 接口允许到 2100（`4102444800`），但 32 位 `time_t` 在 2038-01-19 溢出；`settimeofday` 强转后可能写入 1901 年 | 按 `sizeof(time_t)` 限制上限 2038；网页日期上限同步；文档已有 2038 风险 |
| P2-19 | `HttpServer.cpp:651-693`、`TimerPage.cpp:320-352` | `/api/status` 返回最后一次渲染的快照（最多 1 s 旧；UI 卡住时更久）。实测 `state.text=19:02:40` 与 `device.localTime=19:02:41` 差 1 s | 请求时用 `now + evaluate` 现算，或明确文档“快照”语义 |
| P2-20 | `HttpServer.cpp:196`、`:427`、`:899`；`WifiManager_task.cpp:89` | 多处 `std::thread(...).detach()` 无 try/catch；线程创建失败会 `std::terminate` | 统一 try/catch 并降级（HTTP 返回 503；WiFi 重试） |
| P2-21 | `HttpServer.cpp:748`、`:858` | 两处限流判断 `>` 与 `>=` 不一致，且 accept 与 handler 之间存在计数竞态，可能短暂超过 8 连接 | 统一 `>=`，在 accept 线程原子预留名额 |
| P2-22 | `ConfigMode.cpp:66-67`、`:79`；`mainLogic.cc:253` | `SoftApManager::setEnable/setSsidAndPwd` 在 UI 线程调用，SDK 若阻塞会冻结 LED/倒计时和长按检测 | 移到后台线程，UI 只显示“启动中” |
| P2-23 | `ConfigMode.cpp:15` | 固定 SSID `TC002-Setup`，多台设备同场地会重名/抢连 | SSID 加设备短 ID 后缀 |
| P2-24 | `WifiManager_task.cpp:139-151` | `notifyConnected` 只通知首次；若首次拿不到 IP，后续 IP 变化不会再提示 | 允许 IP 从空到非空时再通知一次 |
| P2-25 | `HttpServer.cpp:611-615` | 旧 `POST /api/wifi` 空密码会保留旧密码（正确），但**没有显式清空密码的接口**（空密码 = 保留） | 增加 `clearPassword:true` 或文档说明 |
| P2-26 | `ConfigMode.cpp:99-105`、`HttpServer.cpp:652` | `/api/status` 轮询会 `touch()` 续期 AP；外部监控可让 AP 永不超时 | 只对配置/保存类操作续期，状态轮询不续期（或单独限频） |
| P2-27 | `ConfigMode.cpp:184-186`、`ConfigMode.h:37` | `password()` 已被 LED 使用；注释说“页面提示”但网页没有 AP 区域 | 网页补 AP 提示，或改注释 |
| P2-28 | `HttpServer.cpp:581-637` | 旧单网络接口把新 SSID `insert(begin())`，与注释“保留顺序”容易误解；且新增网络没有 `touch()` | 明确“新网络置顶”的语义；补 `touch()` |

---

## 5. 长按注入问题分析（重点）

### 5.1 已有证据

- 两次注入（旧 `/tmp` 构建 18:51、当前 `/res` 构建 19:02）日志都是：
  - `status=1` → **18 ms / 22 ms** → `status=0`；
  - 没有 `middle long press detected`；
  - 没有 `SoftAP config mode on` / `ConfigMode: enabling SoftAP`；
  - `/api/status` 的 `apMode=false`、`apIp=""`。
- 代码要求：`mainLogic.cc:25` `kLongPressMs=3000`；`:239-241` 在 1 s UI tick 中判断 `monotonicMs32()-sMiddleDownMs >= 3000`；`:245` 置 `sMiddleLongHandled=true`；`:253` 才调用 `ConfigMode::enter()`。
- `KeyManager` 只发 press/release（`KeyManager.cpp:218-233`），没有长按逻辑；0x69 在 `KeyManager.h:18` 就是 `E_KEYCODE_MIDDLE_BUTTON`，映射正确。
- 因此：**不是 KeyManager 没收到、不是映射错、不是 ConfigMode 没被调用——是根本没按住 3 s。** 18–22 ms 的 `up` 必然来自注入侧（脚本/工具）的一次额外写入；Linux 4.9 的 evdev `release` 不会补发按键释放（4.9 `evdev.c` 无 key_down 批量释放逻辑），所以不能把锅甩给内核 close。

### 5.2 为什么会有 up

按可能性排序：

1. 注入脚本先写 `keydown.bin`，紧接着又写 `keyup.bin`（`build/keydown.bin`、`build/keyup.bin` 两个文件都在，均为 32 B = `input_event` + `EV_SYN`）；
2. 用了 Android `input keyevent`/`input keyevent 21`（`KEYCODE_DPAD_LEFT` 映射到 Linux `KEY_LEFT=0x69`），该命令本身合成 down+up；
3. 测试脚本的 cleanup/trap 在退出时补发了 up；
4. 其它进程也在写同一个 event 节点。

无论哪一种，**“只注入 down”这个前提在日志上不成立**。

### 5.3 最小验证步骤（不改代码）

前置：确认当前跑的是含新代码的构建（现在 `/res` = `7A762407`（v2.1.2），日志行号 124 已证明）；`/tmp` 必须为空，避免误跑旧库。

1. 清日志并起过滤：

```powershell
$adb = ".\tools\platform-tools\adb.exe"
& $adb -s 192.168.31.124:5555 logcat -c
& $adb -s 192.168.31.124:5555 logcat -v time | Select-String -Pattern "middle|ConfigMode|SoftAP|HttpServer"
```

2. **只发 down，不发 up**（两条命令分开；第一条执行后等 6 s 再做别的）：

```powershell
# 需要先把 keydown.bin push 到 /data/local/tmp（如果还没有）
& $adb -s 192.168.31.124:5555 shell "cat /data/local/tmp/keydown.bin > /dev/input/event67"
```

   注意：
   - **不要**使用 `input keyevent`（它必然 down+up）；
   - **不要**在同一个 shell 里紧接 `keyup.bin`；
   - 若担心 close 行为，可只 `cat` 一次并立即开始轮询；4.9 evdev 不会因 close 补 up。

3. 按住期间（0–6 s）每 200 ms 轮询：

```powershell
1..30 | ForEach-Object {
  $s = (Invoke-WebRequest -UseBasicParsing http://192.168.31.124:8080/api/status).Content | ConvertFrom-Json
  "{0} apMode={1} apIp='{2}' text={3}" -f $_, $s.device.apMode, $s.device.apIp, $s.state.text
  Start-Sleep -Milliseconds 200
}
```

4. 看日志，期望顺序：

```text
mainLogic: middle key status=1          （只出现一次）
mainLogic: middle long press detected (~3000–4000 ms)
mainLogic: SoftAP config mode on ssid=%s
ConfigMode: enabling SoftAP ssid=TC002-Setup
ConfigMode: SoftAP ready ssid=TC002-Setup ip=...
```

   然后 `/api/status` 应出现 `apMode=true`，`apIp` 非空；6 s 后再发 `keyup.bin`（如果你要退出，则等 `AP OFF`/超时）。

5. 若日志只有 `status=1`、没有 long press：
   - 先看 `state.text` 是否每秒在变（证明 UI tick 在跑）；
   - 若 `state.text` 也停住，说明 UI 线程被阻塞（SoftAP/flash/渲染），长按检测被饿死——这是 P2-4/P2-22；
   - 若 `state.text` 正常，检查 `sMiddleDownMs`/`monotonicMs32`（代码层面）和是否有第二个进程写 event67。

6. 若 `apMode` 曾短暂 true 又变 false：
   - 查 `ConfigMode: SoftAP enable error`（`ConfigMode.cpp:125-129`）或 `SoftAP timeout`；
   - 查 `getprop persist.softap.on`、`persist.sys.softap.ssid`、`ifconfig`/`ip addr`、`logcat` 中 hostapd；
   - 这是 AP+STA/驱动不支持并发的信号，按 P1-3 处理。

7. 若 `apMode=true` 但 `apIp` 一直空：
   - 状态可能卡在 `ENABLING`/`DISABLED`（P2-2）；
   - 用手机扫描 SSID 是否为 `TC002-Setup`；进入 AP 后 LED 应显示两行（SSID/`12345678`）10 s；也可查 `README.md:54-56` / `native/MultiTimer/README.md:68-70`；
   - 浏览器打开 `http://192.168.100.1/` 应 302 到 `:8080`（以 `apIp` 实测为准）。

8. 若 `apMode` 正常但手机连不上：
   - 先确认 AP 密码是否真的是 `12345678`（`setSsidAndPwd` 是否生效）；
   - 检查 AP 网段/`getIp()`；用 `getevent`/手机扫描确认 SSID；
   - 这是 P1-1 的验收部分。

### 5.4 短按与长按冲突

- 代码在 release 时若 `sMiddleLongHandled` 为 true 就吞掉短按（`mainLogic.cc:131-136`），正常。
- 但按住 3.2 s、在 4 s tick 之前松手：`sMiddleLongHandled` 仍 false，release 会走 `showMiddleStatus()`，显示 IP——用户以为长按失败。建议 release 时也按 `now-down >= 3000` 判定，或把 hold 检测移出 1 s tick。

---

## 6. 逐项功能审查

### 6.1 倒计时 / 时钟严格绑定 — 通过，有两点说明

- `TimerPage::gatherFrame()`（`TimerPage.cpp:98-141`）每次调用都 `time(NULL)`（`:100`），然后 `mEngine.evaluate(now)`（`:129`）；`tick()`（`:143-147`）由 `mainLogic.cc:227-236` 的 1 s 定时器驱动；`draw()` 也走同一路径。
- 未发现任何“剩余秒数递减”缓存；`TimerEngine::evaluate` 用 `epoch-now` 现算（`TimerEngine.cpp:69-88`）。
- `/api/status` 是最后一次渲染的快照（`TimerPage.cpp:320-352`），不是每秒实时计算；实测 `state.text` 比 `device.localTime` 慢 1 s。**显示严格绑定成立，但 API 不是严格实时**（P2-19）。未来分屏如果也走 `gatherFrame` 模式即可；不要在页面里缓存递减。
- UI 线程被 flash（`TimeKeeper::tick`）或 SoftAP 阻塞时，屏幕会短暂停格，但恢复后按当前时钟重算，不会累积漂移（P2-16/P2-22）。

### 6.2 多 WiFi 列表

| 审查点 | 结论 | 证据 |
|---|---|---|
| GET 不回传密码 | **通过** | `HttpServer.cpp:460-466` 只回 `ssid/enabled/hasPassword`；真机响应无密码 |
| 空密码保留原密码 | **通过（列表 + 旧接口）** | 列表 `:560-568`；旧接口 `:611-615`（18:58 修复） |
| 空列表 | 允许且不回落；有离线风险 | `HttpServer.cpp:496`；`WifiStore.cpp:351-357`；P1-4 |
| 重复 SSID | API 400；文件解析去重 | `HttpServer.cpp:526-531`；`WifiStore.cpp:301/325` |
| 8 上限 | API/存储都限制 | `HttpServer.cpp:496`；`WifiStore.h:33`；`WifiStore.cpp:418` |
| 旧单对象迁移 | 可读，但不回写 v2 | `WifiStore.cpp:333-339`；无写回逻辑 |
| 保存失败原子性 | tmp+fsync+rename，失败保留旧文件 | `WifiStore.cpp:193-215`、`:417-438`；无目录 fsync（P2-10） |
| 重连时序 | 800 ms 后 reconnect；列表逐个 30 s；AP 时先退出 | `HttpServer.cpp:425-451`；`WifiManager_task.cpp:121-137/195-231`；最坏 4 分钟（P2-11） |
| 系统自动连接优先 | 应用列表顺序/停用只在系统自动连接失败后生效 | `WifiManager_task.cpp:329-340`；文档需说明（P1-4） |
| GET 失败 | 返回 ok+空，网页可能误判 | `HttpServer.cpp:453-480`；P2-7 |

### 6.3 SoftAP 配网

| 审查点 | 结论 | 证据 |
|---|---|---|
| 生命周期 | `enter/exit/requestExit/tick` 已具雏形；`enter` 先置 `mActive` 后确认状态 | `ConfigMode.cpp:44-169`；P2-1 |
| 超时 | 默认 15 min，HTTP 活动 `touch()` 续期 | `:17`、`:99-105`、`:158-168`；状态轮询也会续期（P2-26） |
| 状态轮询 | 1 s `tick`；只处理 `ENABLE_ERROR`；`DISABLED/ENABLING` 可能拖到超时 | `:107-169`；P2-2 |
| 线程安全 | `mActive/mExitRequested` 原子；`mSsid/mIp/mOnReady/mStartedMs` 互斥；`SoftApManager` 只在 UI 线程（`enter/exit/tick`），HTTP 只 `touch/requestExit` | `ConfigMode.h:48-56`；`ConfigMode.cpp:57-63/81-85/103-105/138-164`；仍存在 UI 阻塞风险（P2-22） |
| 长按检测 | KeyManager 无长按；UI 1 s tick；3–4 s 生效；UI 卡住会漏检 | `KeyManager.cpp:218-233`；`mainLogic.cc:238-265`；P2-4、§5 |
| 短按冲突 | 长按后 release 被吞，正常；3.2 s 松手会误判短按 | `mainLogic.cc:131-136`；§5.4 |
| 端口 80 | 开机常开；bind 失败只 warning；无重试/无并发上限 | `HttpServer.cpp:175-198`、`:901-905`；P2-6 |
| 302 地址 | 用 accepted socket 的 `getsockname()`，取本端地址，**正确**；STA 网络实测 `Location: http://192.168.31.124:8080/`；AP 下应返回 AP IP | `HttpServer.cpp:113-127`、`:138-142`；实测通过 |
| AP+STA | v2.1.2 已修：连接循环 AP 检查（`:210-213`）、`exit()` 顺序（`ConfigMode.cpp:72-90`）、外层暂停（`:318-322`）；仍剩 `reconnect()` 跨线程 `wm->disconnect()` 与 `mReconnectRequested` 双重重连 | `WifiManager_task.cpp:121-137/210-213/318-322`；P1-3 |
| AP IP | 文档实测 `192.168.100.1`；`ConfigMode::ip()` 由 `SoftApManager::getIp()` 填充；端口 80 的 `getsockname` 在 AP 下应返回同一 IP | `README.md:55`、`native/MultiTimer/README.md:69`；`ConfigMode.cpp:131-155`；`HttpServer.cpp:113-142` |
| 密码/SSID | 固定 `TC002-Setup` / `12345678`；v2.1.2 起 LED 显示两行 10 s，README/native README 也写了；固定口令可被同场地猜测 | `ConfigMode.cpp:15-16`；`mainLogic.cc:255-257`；`TimerPage.cpp:199-206`；P1-1 |

### 6.4 60 s NTP / 延迟落盘

| 审查点 | 结论 | 证据 |
|---|---|---|
| 60 s 节流 | **通过**；在线每 30 s 检查、60 s 放行 | `WifiManager_task.cpp:33-34`、`:236-239`；日志 10:51:49 / 10:52:49 |
| 失败重试 | 失败不更新 `mLastTimeSync`，下轮重试 | `:236-239`、`:258-260`；但 `last=0` 时每 30 s（P2-13） |
| `noteSynced` 不写 flash | **通过**；只更新内存 | `TimeKeeper.cpp:169-179` |
| 首次有效 `saveState` | `!wasValid` 时写一次；但从 flash 恢复的系统时间“valid 但 lastSync=0”不会立即写，语义略偏 | `WifiManager_task.cpp:240-246`；P3 |
| 日志频率 | 成功每 60 s 一条 `LOGD`；失败每 30 s 一条 `LOGW` | `:237`、`:250-257`；P2-13 |
| 自定义 NTP | `ntpServer` 已进配置/schema/web；启动加载和保存热应用都 `setServer`；失败回退内置 | `ConfigStore.cpp:366-381/463-467`；`SntpClient.cpp:131-167`；`mainLogic.cc:97/166` |
| 跳时影响 | 每 60 s 若偏差 >1 s 跳变，可能重复/跳过一秒 | `SntpClient.cpp:169-188`；P2-15 |
| 与 flash 磨损 | `noteSynced` 不立即写；`tick()` 每 300 s 写 | `TimeKeeper.cpp:18/186-201`；但 UI 阻塞（P2-16） |
| 文档一致 | **部分更新**：README/native README 的 v2.1 段落正确；HANDOFF/FLASH/native 旧段落仍写 6 小时/92，`STATE-v21` 死链 | §7 |

### 6.5 HTTP / WiFi 线程安全、锁、32 位 ARM

- **锁顺序**：`PageManager::mMutex → TimerPage::mMutex`（`navigateTo → draw`）；`ConfigMode::mMutex` 与 `TimerPage` 不嵌套；`TimeKeeper::mMutex` 内只调 `StoragePreferences`；未发现反向嵌套/死锁。
- **已修复的并发点**：SoftAP 的 `enter/exit/tick` 只在 UI 线程；HTTP 只 `touch/requestExit`（原子/互斥），`ConfigMode` 不再被 HTTP 直接驱动。
- **仍存在的并发点**：`reconnect()` 从 HTTP 派生线程调 `WifiManager::disconnect()`，与 bootstrap 线程的 `connect/isConnected` 并发（P1-3）。
- **原子类型**：`std::atomic<bool>`、`std::atomic<unsigned long>`（`WifiManager_task.h:55-61`、`mainLogic.cc:27-29`、`ConfigMode.h:49-50`）在 32 位 ARM 上无 libatomic 依赖；未使用 `atomic<long long>`。`monotonicMs32()` 的无符号减法在 49.7 天回绕内正确。
- **内存/线程**：HTTP 每连接一线程（上限 8，`HttpServer.cpp:38/858`），redirect 每连接一线程**无上限**（P2-6）；`detach` 线程未统一 try/catch（P2-20）；`std::function`/`std::vector` 均在锁内拷贝，未发现明显泄漏。
- **`std::thread` 与 `WifiManager` 的 SDK 调用**是最需要真机压测的部分（建议 10 并发 + AP 切换 + 保存列表）。

---

## 7. docs 一致性问题（只列问题）

> 文档在 19:04–19:09 被更新：`README.md` 与 `native/MultiTimer/README.md` 的 **v2.1 段落已经补齐**（多 WiFi、AP、80→8080、NTP、102/62 都有），`docs/STATE-2026-10-22-v21.md` 于 19:08 创建。下面只列仍然不一致/缺失的地方；`docs/STATE-2026-10-22-v2.md`、`docs/RESEARCH-*.md` 是历史快照/调研，仅在会误导现场时列出。

| # | 文档位置 | 文档说法 | 实际 |
|---|---|---|---|
| 1 | `docs/STATE-2026-10-22-v21.md`（19:08 新建） | 文件已存在，但内容仍是 v2.1.1：`/res` = `A9AE6B05...`、`targets` 为空 | 19:06 已刷 v2.1.2：`/res` = `7A762407…`；19:06 已保存 3 个目标（`/api/status` 为 countdown）。快照需更新到 v2.1.2 |
| 2 | `README.md:128` | “SNTP 成功或网页 `POST /api/time` 对时后，系统时钟立即更新并持久化” | 网页对时 `setUtc()` 确实立即持久化；**SNTP 只 `noteSynced()` 更新内存**，不立即写 flash（`TimeKeeper.cpp:169-179`）；同仓库 `native/MultiTimer/README.md:128-130` 已写对，二者矛盾 |
| 3 | `docs/HANDOFF-native.md:211`、`:213`、`:283` | `noteSynced()` 立即保存；成功对时间隔 6 小时 | `noteSynced()` 延迟落盘；间隔 60 s（`WifiManager_task.cpp:34`、`:236-239`） |
| 4 | `native/MultiTimer/README.md:678` | “成功对时间隔 6 小时” | 60 s；与同文件 `:120-125` 的 v2.1 描述矛盾 |
| 5 | `native/MultiTimer/README.md:340-350` | HTTP API 表只有 `POST /api/wifi`，没有 `GET /api/wifi` / `POST /api/wifi/list` | 代码已有（`HttpServer.cpp:827-832`） |
| 6 | `native/MultiTimer/README.md:354-365`；`docs/HANDOFF-native.md:320+`；`docs/DESIGN-v2.md:145-159` | `device` 只有 ip/ssid/wifi/uptime/timeUtc/timeValid/tz/localTime/lastSyncUtc | 新增 `apMode`/`apSsid`/`apIp`（`HttpServer.cpp:309-311`） |
| 7 | `native/MultiTimer/README.md:275-289`、`:310`；`docs/DESIGN-v2.md` §3 | 配置 schema/字段顺序没有 `ntpServer` | `ConfigStore::toJson` 总输出 `ntpServer`（`ConfigStore.cpp:466`），web 也提交（`web-src/index.html:2076`）；`DESIGN-v2` 是 v2 契约，v2.1 兼容扩展没有单独文档（见 #1） |
| 8 | `docs/DESIGN-v2.md:141` | `POST /api/wifi` **不变** | 旧接口语义已变为“保留其他网络 + 空密码不清空”（`HttpServer.cpp:580-637`）；新增列表接口 |
| 9 | `docs/DESIGN-v2.md:92`；`docs/HANDOFF-native.md:235-236`；`native/MultiTimer/README.md:101` | 中键覆盖层固定 **6 秒**；native README 写“长按进入显示 AP SSID **8 秒**” | 短按 6 s、AP 下短按 8 s、AP ready 10 s、**进入 AP 10 s（v2.1.2 显示 SSID+密码两行）**、退出 4 s、失败 6 s（`mainLogic.cc:50-56/146/250/255-257/263`）；README.md:85-86 只写短按/ready，native README:101 的 8 s 已过时 |
| 10 | `docs/HANDOFF-native.md:301`；`native/MultiTimer/README.md:337-338` | “无限流” / “只监听 0.0.0.0:8080，无鉴权、无 TLS” | 下一行又写“最多 8 个并发连接”（代码 `kMaxActiveClients=8`，`HttpServer.cpp:38`）；现在还常开 80 端口 302（`HttpServer.cpp:175-198`、`:901-905`），README:68-70 已写对，HANDOFF/native 的旧段落没写 |
| 11 | `native/MultiTimer/README.md:789`；`docs/HANDOFF-native.md:435`；`docs/FLASH-RUNBOOK.md:415`、`:502` | 单测 **92/92**、网页 **55/55**、WifiStore 13 项 | 本次实跑 **102/102**、**62/62**；WifiStore 已含 `parseJsonNetworks`，19 项（`tests/README.md` 已更新到 102/62，但源码快照时间仍是 16:xx） |
| 12 | `docs/FLASH-RUNBOOK.md:119`、`:211` | `/res/lib/libzkgui.so` **435,820 字节**，SHA256 `123E1848…` | 当前 `/res` 与 `dist/lib` 是 **480,876 字节**；v2.1.1 为 `A9AE6B05…`（19:00），v2.1.2 为 `7A762407…`（19:05）；`update.img` v2.1.2 = `A3C1655A…`。README:179-188 也仍是 v2.1.1 哈希 |
| 13 | `README.md:19` | FLASH-RUNBOOK “含 v2 与 v2.1 记录” | `docs/FLASH-RUNBOOK.md` 没有 v2.1.1/v2.1.2 的镜像哈希/执行记录（仍是 435,820/92），与 README 其他 v2.1 段落不一致 |
| 14 | `ConfigMode.h:37` | 热点密码“文档与页面提示使用” | README/native README + LED 都有；**网页没有** AP 区域/密码提示 |
| 15 | `WifiManager_task.cpp:348`；`TimeKeeper.h:25` | 代码注释“内部另有 6 小时成功节流”；`noteSynced` “并持久化” | 60 s；延迟落盘（P2-12/P2-17） |
| 16 | `docs/RESEARCH-tc002-wifi-deploy.md:394` | 建议 SoftAP **180 s** 无配网自动关 | 代码默认 **15 min**（`ConfigMode.cpp:17`）；这是历史调研建议，但容易被当成现状 |
| 17 | `README.md:20`、`:209`；`native/MultiTimer/README.md:18` | 链接到 `docs/STATE-2026-10-22-v21.md` | 文件已创建，但内容仍是 v2.1.1（见 #1）；链接本身不再死 |
| 18 | `README.md:58` / `native README:16` | “物理长按尚未由用户确认” | **这是诚实的**，与本文 P0-2 一致；不算问题，但发布说明必须保留 |
| 19 | `README.md:131`；`native/MultiTimer/README.md:15` | “当前设备的 targets 为空” | 19:06 已保存 3 个目标（19:30/20:00/20:30），`/api/status` 已 `mode=countdown`；文档需更新 |
| 20 | `README.md:172-188`；`native/MultiTimer/README.md:11-16`、`:617/693/728-729/1036` | 构建/镜像/固化哈希为 v2.1.1 `A9AE6B05…` / `9378387A…` | 19:05 已构建/刷入 v2.1.2：`7A762407…` / `A3C1655A…`；README 需更新 |

**仍然缺失、建议补的文档**：

- `docs/STATE-2026-10-22-v21.md`（19:08 已创建，需更新）—— 改为 v2.1.2 的最终哈希（`7A762407…` / `A3C1655A…`）、19:06 刷机步骤、3 个目标已保存、AP/IP 实测、长按待确认项；
- v2.1 设计增补（或更新 `DESIGN-v2.md` 的“兼容扩展”节）：`ntpServer`、`apMode/apSsid/apIp`、多 WiFi 列表 schema/语义、AP 状态机/超时/80→8080；
- `docs/FLASH-RUNBOOK.md` 的 v2.1 固化记录：v2.1.1 `A9AE6B05` / `9378387A`（19:00）与 v2.1.2 `7A762407` / `A3C1655A`（19:05–19:06）两轮、`/tmp` 为空、`/data` 保留、AP 待验证；
- `docs/HANDOFF-native.md` 的 v2.1 更新：6 小时→60 s、立即落盘→延迟落盘、API 表、device 字段、线程/AP 说明；
- `tests/README.md`：已更新到 102/62/WifiStore 19 项；源码快照时间/哈希可再刷新（P3）。

**已确认写对的 v2.1 段落**（不需要改）：`README.md:28/54-61/68-70/137-151/198-206`；`native/MultiTimer/README.md:65-79/120-130`。

---

## 8. 建议的测试命令（按顺序）

### 8.1 主机（本次已跑）

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1
# 期望 Summary: 102 cases | passed 102 | failed 0

node native\MultiTimer\tools\check_web.js
# 期望 62/62 通过
```

### 8.2 构建 / 镜像（下次改动后）

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1 `
  -Workspace build\v211-verify -LogPath build\v211-verify.log
# 期望 Build Finished. 0 errors, 0 warnings.

# web-src 有改动时（会改 editor_gz.h，需 owner 执行）：
python .\native\MultiTimer\tools\build_web_asset.py
node .\native\MultiTimer\tools\check_web.js

powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\make-image.ps1 `
  -SoPath dist\lib\libzkgui.so -MainFtu native\MultiTimer\ui\main.ftu -OutDir dist
# 核对 dist\build-info.txt：input_so_sha256 = 当前源码构建哈希
# 当前：7A76240720B558CA...，update.img A3C1655AD16C27A1...
```

### 8.3 真机 API（读优先，写前先备份 `/data`）

```powershell
$ip = "192.168.31.124"
(Invoke-WebRequest -UseBasicParsing "http://$ip`:8080/api/status").Content
(Invoke-WebRequest -UseBasicParsing "http://$ip`:8080/api/config").Content
(Invoke-WebRequest -UseBasicParsing "http://$ip`:8080/api/wifi").Content
(Invoke-WebRequest -UseBasicParsing "http://$ip`/").StatusCode   # 期望 302
```

写接口用例（先 `adb pull /data/multitimer.conf`、`/data/multitimer.wifi` 备份）：

```powershell
# 1) 列表：2 个网络、密码留空保留
# 2) 重复 SSID -> 400
# 3) 9 个 -> 400
# 4) 空列表 -> 200，然后确认设备是否还能联网（P1-4）
# 5) 旧 POST /api/wifi 只发 ssid -> 确认原密码未被清空
# 6) POST /api/time 前后 1 小时 -> /api/status.remainSeconds 立即变化
# 7) POST /api/config 改 tzOffset -> 屏幕/接口立即变化
```

### 8.4 长按 / SoftAP

按 §5.3 执行。验收判据：

- 只出现一次 `status=1`；
- 约 3–4 s 出现 `middle long press detected`；
- 出现 `SoftAP config mode on` + `ConfigMode: enabling SoftAP`；
- `apMode=true`、`apIp` 非空；
- 手机能搜到 `TC002-Setup` 并用正确密码连接（进入 AP 后 LED 应显示两行 SSID/密码 10 s；README/native README 也有）；
- `http://192.168.100.1/`（以 `apIp` 为准）→ 302 → `:8080`；
- 在 AP 页面保存 WiFi 列表 → AP 退出 → STA 恢复；
- 长按 3 s 退出 AP；`apMode=false`，STA 自动重连。

### 8.5 NTP / 时钟

```powershell
& $adb -s 192.168.31.124:5555 logcat -v time | Select-String "SNTP|noteSynced|clockState"
# 期望 60 s 一组；失败时不要每 30 s 刷屏

# 阻塞 UDP 123 / 填错 ntpServer -> 观察失败日志与回退内置
# 冷启动 -> TimeKeeper: restored system clock from flash ... + WiFi 后 SNTP 校正
# POST /api/time +3600 / -3600 -> 屏幕立即变化；重启后仍保留
```

### 8.6 并发 / 长稳

- 10 个并发 GET `/api/status`、`/api/wifi`、`/api/config` + 2 个并发 `POST /api/wifi/list`（用不同列表）→ 无崩溃、无 FD 泄漏、无 503 雪崩；
- AP 开/关 20 次 + 保存列表 20 次 → 检查 `logcat` FATAL、AP 是否卡在 `apMode=true`、STA 是否恢复；
- 24 h 挂机：LED 与 `/api/status` 时间不漂移、flash 写入频率约 300 s/次、内存不增长。

---

## 9. 刷机建议

1. **本次镜像已经刷入且可用**：`/res` = `7A762407…`（v2.1.2，480,876 B）；`/tmp` 空；`/data` 保留；HTTP 200；端口 80 302；WiFi 仍连 `JasonYiL`；19:30 目标已保存。**没有变砖证据。**
2. **不要再刷“下一版”**，直到：
   - P1-1（固定弱口令）、P1-2（CSRF/鉴权）、P1-3（AP/STA 竞态剩余项）、P1-4（空列表）修完；
   - 长按真按住测试通过（§5.3）；
   - docs 至少补上 AP 密码/多 WiFi/端口 80/NTP，测试数量更新；
   - 重新构建并从 `dist/build-info.txt` 核对 `input_so_sha256`；`update.img` 与源码哈希一致后再 dry-run → flash。
3. **19:30 前不要重启/刷机**：先把三个目标保存并确认；19:25–20:35 是目标触发窗口，刷机会重启、错过验收。
4. **刷机前备份 `/data`**（只读 pull，不要清空）：

```powershell
& $adb -s 192.168.31.124:5555 pull /data/multitimer.conf   build\backup\multitimer.conf
& $adb -s 192.168.31.124:5555 pull /data/multitimer.wifi   build\backup\multitimer.wifi
& $adb -s 192.168.31.124:5555 pull /data/preferences.json  build\backup\preferences.json
```

5. **回滚**：长按 USB-C 旁复位键约 5 s 会恢复官方固件并清空 `/data`；`deploy-tmp.ps1 -Restore` 只清 `/tmp` 覆盖层，不能恢复已替换的 `/res`。
6. 若 AP 实测失败但 STA 正常，**不要**为了“修 AP”盲目改 `WifiBootstrap`/`ConfigMode`；先按 §5.3 区分“没按住” vs “AP enable error” vs “AP+STA 冲突”。

---

## 10. 快照哈希（复核用）

```text
HttpServer.cpp   C05DB7517E380E52...
ConfigMode.cpp   4A088032A092C1F7...
WifiManager_task B3D7407942F08AB4...
mainLogic.cc     D0D2269A3BCC0696...
TimerPage.cpp    41FD7F852D9AC8DC...
WifiStore.cpp    CFB4B05355AAB04E...
TimeKeeper.cpp   A7730D687D43AE7A...
SntpClient.cpp   1B6BEE1DDEC5...
ConfigStore.cpp  0B0CB1514A2A...
TimerTypes.h     9CAF2A653F85...
libzkgui.so      7A76240720B558CAA69FC7A9ABC6DBE2B66860B0FEE3FE0B738A0C229EB5DD9B
update.img       A3C1655AD16C27A18D6D25DAC1835D265EB8256CD7E802EABFB99923132FE9B8
```

> 若上述文件哈希变化，本文的“已修复/仍存在”结论需要重新核对。审查期间工作区/设备被并行修改多次：v2.1.1（A9AE6B05/9378387A，19:00）→ v2.1.2（7A762407/A3C1655A，19:05）；18:58 之后的修复包括旧 `/api/configmode` 密码回显/密码日志/旧接口空密码清空、AP 连接循环检查、`exit()` 顺序。
