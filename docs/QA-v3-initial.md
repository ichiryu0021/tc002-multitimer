# TC002 MultiTimer v3 — QA 报告（task-1）

- 执行者：qa-v3（Team QA Lead）
- 执行时间：2026-10-22 19:44:05 – 19:53:30（Asia/Shanghai），主机侧
- 范围：构建 / 主机单测 / 网页自检 / v3 集成静态审查（只读源码，未改任何源码）
- 禁止项遵守：**未部署、未刷机、未重启设备**；设备 19:30/20:00/20:30 目标倒计时期间只做主机验证。
- 报告写作用域：仅本文件 `build/qa-v3-report.md`。

## 0. 结论摘要

| 项目 | 结果 |
|---|---|
| 主机交叉构建（Z21/ARM） | ✅ IDE exit 0，`0 errors, 0 warnings`，`libzkgui.so` 718,436 B |
| 主机单测 | ✅ 209 cases / 209 passed / 0 failed（exit 0） |
| 网页自检 | ✅ 90/90 通过（exit 0） |
| P0（阻断/崩溃/数据损坏） | **0** |
| P1（发布前必须修） | **0** |
| P2（建议修/风险/文档-实现一致性） | **10** 项，见 §7（无阻断项；其中 P2-1/P2-2 为 API 契约一致性，建议发布前处理） |
| 真机验收项 | ⚠ 未验证（本窗口禁止部署；清单见 §8） |

一句话：**主机侧三绿（构建 0 errors、单测 209/209、网页 90/90），倒计时严格绑定时钟/v2→v3 迁移/hybrid 路由/HTTP v3 契约静态审查通过，未发现 P0/P1；建议 20:35 后用本报告 §9 的最终产物做 /tmp 真机联调，再决定是否固化。**

## 1. 源码快照（QA 基线）

工作区 HEAD：`b2a4b2706a9b93668e9d3a892e00a372c8bbea8e`（v2.1.2，v3 变更尚未提交；`git status` 40 项，含 18 个修改文件 + 22 个新增路径）。

v3 源码最终冻结时间：**2026-10-22 19:48:37**（最后一笔为 `native/MultiTimer/src/net/TlsClient.cpp` 的 TLS 加固）。19:48:37 之后至 19:53:18 复查无任何源文件变化（不含文档 .md）。

- 源码清单哈希（`native/MultiTimer/src` 73 个文件，按路径排序后逐文件 SHA256 再整体 SHA256）：`AC0B667100342FB6976B54C7A31280958F5E910F2C54C009F82EDA051B2CE915`
- 冻结前备注：Lead 19:43:19 宣布冻结后，`TlsClient.cpp` 仍有两次有效改动（19:46:12 加 OpenSSL 错误诊断；19:48:37 加 SIGPIPE 防护 + 强制 TLS1.2/密码套件）。**19:44:18 的第一轮构建已作废**，本报告全部数据来自 19:51:46 的第二轮构建（已核对 build workspace 快照与当前源码一致，唯一差异是 IDE 自身改写的 `.settings/language.settings.xml`，非源码）。

关键文件 SHA256：

| 文件 | SHA256 |
|---|---|
| `src/pages/PageHost.cpp` | `B091B671EE7E5B6EC9EFFF2C22C22870A20F9310941C812B1E35C04A415BA532` |
| `src/net/HttpServer.cpp` | `E49E558EDF80E8E1E01A2717ADB69014A70EFCE3DB0C173494B9F949BEC79C48` |
| `src/live/LiveService.cpp` | `744F729B097C5157B92BD7F6663133A4C91CD52ADE785A5D6269988334D2FB0A` |
| `src/net/TlsClient.cpp` | `C088228721783740052A16B90798DE53456089579E8A724EB21BD4101E07469D` |
| `src/config/ConfigStore.cpp` | `6413F15590215F7EE964174F0323FEE69ED7EEBDA7AE1D459DC222F50660D829` |
| `web-src/index.html` | `08D784B49C3B3B10DF092D411D844CE908657B9CC183BFD585D5DAF588CF219A` |
| `tools/check_web.js` | `597E16C3D6D65CA98AE44A330FA7AF3C8C57464E02429DC22EF661A81D9605E3` |

## 2. 构建验证

命令（独立 workspace 与独立日志，未复用 v2/Lead 的 workspace）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 `
  -Workspace build\qa-v3-workspace -LogPath build\qa-v3-build.log
```

结果（19:51:46 → 19:51:56，19.2 s）：

- IDE exit code：`0`；日志末行 `Build Finished. 0 errors, 0 warnings. (took 5s.460ms)`；`Linking libzkgui.so` 成功
- 产物：`build/qa-v3-workspace/MultiTimer/Release/libzkgui.so`
  - 大小 **718,436 B**（701.6 KB；v2.1.2 为 ~700 KB 级，`/res` 8 MiB 预算无压力）
  - SHA256 **`E92CAFD058F9B67A33A2D223178852036E8C3CCCF1FC1A9B84B26EDE616F6443`**
- 构建日志：`build/qa-v3-build.log`，16,373 B，SHA256 `CCDEDEA9EB2DE1D0E6870A293378C9274FFA00EE31F501BB313B294A8A934089`；`error:` 行 0、`warning:` 行 0（GCC 的 `note: parameter passing ... changed in GCC 7.1` 是 ABI note，不计 warning）
- 日志中 `cann't find any dirctory / Duplicate identifier ... manifest` 为 FlyThings IDE 启动噪音，与代码无关
- 可复现性：19:44 第一轮构建（TLS 加固前）产物 714,340 B / `2418B2935FF145987A2B13214E198B34915A2B9E44ABB7F9DCC86E60D3F1E9F4`，与 Lead 在 `build/v3-dev-workspace`（19:41:14）的产物**逐字节一致** → 构建可复现；当前 Lead 的 v3-dev 产物仍是加固前版本，做真机 /tmp 联调请以本报告产物（718,436 B / `E92CAF…`）为准或按冻结源码重编

## 3. 主机单测

命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1
```

结果（19:51:46 开始，MSVC 14.44 / `std:c++14`，从源码重新编译；`tests/out/multitimer_tests.exe`，1,956,864 B，SHA256 `D6916FB2B4630CBABA0042FB68A0A719ED368DEE4ADACAD430E1C6BF53B21815`）：

```text
Summary: 209 cases | passed 209 | failed 0
RESULT: ALL TESTS PASSED
[run] exit code: 0 (0 = all cases passed)
```

分组（按日志统计）：bilibili 17、config 54、engine 19、json 10、live_format 24、renderer 42、timemath 24、wifi 19 = **209**。

v3 新增/扩展用例覆盖（均通过）：

- `test_page_renderer.cpp`（42）：clock scale2/无效时间/时区、分屏上下几何与**同一 now**（`renderer_split_page_uses_same_now_for_both_halves`）、无目标 date/blank、预警仅下半、≥100h 小字体、live 两行几何/平台色/数值压缩/0 值有效/失败与 WAIT `--`/STALE dim/空槽/NO LIVE、label fit 阶梯、横幅/覆盖层、越界页安全
- `test_config_store.cpp`（54）：v3 往返/字段顺序/转义、v2→v3 迁移**保留 tz/warn/colors/targets/ntpServer** 并补 dim/display/live/3 页（`config_load_v2_migrates_to_v3`）、迁移保存幂等、v1 链式迁移、页面/房间/ID/标题/label/url/refresh/timeout 上限与 400 语义、`version=4` 保留原文件、缺文件/损坏文件回默认 4 页
- `test_live_format.cpp`（24）：数值压缩边界（0/9999/1e4/1e7/1e10/负数/LLONG_MAX）、w→y 进位、status 文本、平台色 B `#FF6699`/D `#FFFFFF`/K `#FF9900`、label 规范化/默认生成、effectiveMetric 继承、cell configured
- `test_bilibili_client.cpp`（17）：room_id 校验、getH5InfoByRoom/get_info JSON 解析、0 值有效、code≠0 错误、chunked/Content-Length/裸 LF/截断/64 KiB 上限
- v2 基线未回归：time_math 24、timer_engine 19、mini_json 10、wifi_store 19 全绿

说明：`TlsClient/HttpClient/LiveService/BilibiliClient` 为 POSIX/平台层，不参与主机单测（未验证项见 §8）；`LiveCache` 是纯 C++ 但未纳入 host 测试（见 P2-5）。

## 4. 网页自检

命令：

```powershell
node native\MultiTimer\tools\check_web.js
```

结果（19:51:46，exit 0）：

```text
PASS 1..90
----------------------------------------------------------
90/90 通过
```

日志：`build/qa-v3-webcheck.log`，SHA256 `DB0B2DCEEB5CDC8CD71E43D0F9A6BFE913F5E2454C6B0D8D2CD1FAF415CE7104`。

覆盖主题：v3 schema 回填/`buildPayload` 精确字段序（#52）、12 页/24 槽位/12 目标最坏 payload < 16 KB（#53）、页面编辑器增删改/排序/默认页/启用（#11–#36）、直播固定 2 槽位/平台指标联动/房间号规则/粘贴解析/重复校验（#38–#49）、live.source off/bridge/hybrid/push 与 URL/refresh/timeout 校验（#50–#51）、v2→v3 网页侧迁移与 v2 只读（#55/#58）、保存链路 POST→GET→静默对时（#62）、聚合校验/500/网络错误/dirty（#59–#64）、目标/时区/设备信息（#66–#73）、多 WiFi（#74–#79）、多时钟地址/`file://`（#80–#81）、XSS（#37/#83）、390px 静态约束（#5）、零外链/零 console error（#1–#8/#88）、模拟器帧几何（#14–#23/#86–#87）。

另核对内嵌资源一致性：`web-src/index.html` 重新 gzip（mtime=0, level 9）与 `src/web/editor_gz.h` **逐字节一致**（`MATCH True`，gzip 45,554 B；index.html 181,687 B）。即设备 `GET /` 返回的编辑器就是自检通过的这版页面。

## 5. 集成静态审查（按任务重点逐条）

### 5.1 每帧一次 `time(NULL)`；分屏上下共用同一 now；无剩余秒递减缓存 ✅

- `PageHost::tick()` 顶部**唯一一次** `time(NULL)`：`src/pages/PageHost.cpp:551-554`（注释即 "本帧唯一一次"）；`Stage stage = mEngine.evaluate(nowUtc)` 与 `PageRenderer::renderPage(view, 0, nowUtc, stage, ...)` 使用同一 `nowUtc`（`PageHost.cpp:606-607, 714`）。分屏上下两半、倒计时、日期均来自这个 `now`。
- `PageRenderer` 为纯函数：不读全局时间（接口注入 `nowUtc`/`Stage`，`PageRenderer.h:85-89`），`renderSplitPage` 用同一个 `stage`/`nowUtc` 渲染上下两半（`PageRenderer.cpp:62-96`）。
- 全仓扫"禁止模式"：`remainSeconds--`、`mLastRenderSec`、`cachedRemain`、`remainCache` 等 **0 命中**；倒计时恒为 `targetEpoch - now`（`TimerEngine::evaluate(now)`，v2 文件未改）。
- `CLOCK_MONOTONIC` 只用于覆盖层/横幅/直播陈旧判定（`PageHost.cpp:21-28`、`LiveService.cpp:51-58`、`LiveCache.cpp:197-215`），不参与时钟/倒计时显示。
- 主机像素测试 `renderer_split_page_uses_same_now_for_both_halves`、`renderer_timezone_change_immediate` 等已钉住该不变量。

### 5.2 250ms 渲染节拍 ✅

- `REGISTER_ACTIVITY_TIMER_TAB {1, 250}`：`src/logic/mainLogic.cc:20-23`；`onUI_Timer` 每 tick 调 `TimerPage::tick()`（`mainLogic.cc:303-318`）。
- `TimerPage::tick → PageHost::tick(monotonicMillis(), surface)`，仅脏帧 `return true` 才 `extractRGB + sendLedData`（`TimerPage.cpp:39-41, 102-108`）；脏帧签名覆盖秒变化/切页/横幅/覆盖层/live 快照（`PageHost.cpp:634-699`）→ 稳态约 1 帧/秒，符合契约 §4。
- 每 4 tick（=1 s）做 `TimeKeeper::tick()`/`ConfigMode::tick()`/延迟迁移（`mainLogic.cc:315-345`），与 v2.1.2 的 1s 周期等价。
- 真机风险：FlyThings 定时器最小间隔需真机确认（DESIGN-pages-v3 §3.3 已列此风险）；未验证项。

### 5.3 v2→v3 迁移保留 targets/tz/colors/ntp/WiFi ✅

- `ConfigStore::load()`：`version=2` → `migrateV2()`（`ConfigStore.cpp:888-946, 1178-1187`）保留 `tzOffsetMinutes/warnSeconds/colors(clock,countdown,warning)/targets/ntpServer`，补 `colorDim`(0x808080)/`display`(date)/`live`(off,30s,3000ms)，`appendMigrationPages()` 补 `p0 clock / p1 clock_countdown / p2 countdown`、`defaultPageId=p0`、**不自动建 live 页**（`ConfigStore.cpp:347-356, 943-944`）；`migrated=true` 时 `mainLogic onUI_init` **立即 save 一次**（`mainLogic.cc:115-121`）。
- 首次全新安装：`defaultConfig()` 4 页（p3 live 2 个空槽、source=off）（`ConfigStore.cpp:1081-1096`）；`load()` 无条件先把 out 置默认 4 页。
- v1→v3 链式迁移：时间无效时保留原文件 + `sPendingConfigMigration`，时间有效后主线程补迁一次（`mainLogic.cc:122-126, 325-344`）。
- WiFi 独立存储（`WifiStore`，v2.1.2 文件未改），迁移不触碰；`/api/wifi*` 路由与逻辑保持。
- 主机测试：`config_load_v2_migrates_to_v3` 断言 tz=-300/warn=25/ntp=pool.ntp.org/3 色/2 targets/pages 3/defaultPageId p0；`config_load_v2_migration_save_is_idempotent`；`config_load_v1_migration_*`、`config_migrated_v1_round_trips_to_v3`。**通过**。

### 5.4 v3 页面/直播配置校验 ✅（与契约 §2 逐项对齐）

- `version` 必须 3，v1/v2 POST 明确 400（`ConfigStore.cpp:690-714`，`kV2NotAccepted` 提示刷新）；`tzOffsetMinutes` 必填 -720..840；`warnSeconds` 0..600 默认 10；`ntpServer` 空或 host[:port] ≤64；`colors` 4 色缺省；`display` date/blank；`targets` ≤12、单项非法丢弃、超限 400；`live.source` off/bridge/hybrid/push 默认 off、`url` 仅 `http://` ≤160 无空格、`refreshSec` 15..300、`timeoutMs` 1000..5000；`pages` 1..12、未知 type 400、id `[a-z0-9_-]{1,8}` 唯一（非法/重复自动生成）、title ≤16 码点、shortTitle ≤8 可打印 ASCII、enabled、metric、live 页 rooms 1..2（platform/room≤40/label≤3/enabled/metric）、同页重复房间后者置空槽、`defaultPageId` 不存在回退第一页。
- 请求体上限：`kBodyMaxBytes=16384`、规范化配置 `kConfigMaxBytes=12288`（`HttpServer.cpp:36-37, 438-442, 1015-1019`）。
- 网页端 `buildPayload`（index.html）与设备 `fromJson` 字段名/取值范围逐项比对一致（version/tz/warn/ntp/colors/display/targets/live/defaultPageId/pages/rooms 同名同语义；live 页固定发 2 槽，设备接受 1..2）。
- 主机 `test_config_store.cpp` 54 例覆盖上述全部边界。

### 5.5 PageHost / 旋钮切页 / 跳过 disabled / 当前页持久化 ✅

- 单个 `PageHost` 持有页表与当前下标，`TimerPage` 退化为 PageBase 薄适配层（注册名仍 `"TimerPage"`）：`TimerPage.h:1-6`、`TimerPage.cpp:22-108`。
- 旋钮：`mainLogic` KeyManager 回调 → `page->nextPage()/prevPage()`（CW 下一页、CCW 上一页），120ms 防重复上报（`mainLogic.cc:168-189`）；`PageHost::stepPage` 循环扫描并**跳过 `enabled=false`**、默认循环（`PageHost.cpp:320-358`）；只有 1 页时无动作。
- 切页：置 `mBannerIndex/mBannerUntilMs=now+1200ms`，1.2s 内显示 `shortTitle` + `i/N`（`PageHost.cpp:348-353, 616-629`；`PageRenderer::renderPageBanner`）。
- 覆盖层优先级最高、横幅次之（`PageHost.cpp:609-632, 708-716`），与契约 §3/§4 一致。
- 热应用：`setConfig` 换配置 + 当前页 id 仍存在则保持、否则 clamp/找 enabled 页（`PageHost.cpp:199-239`）；`LiveService::noteActivePage` 在锁外调用（锁顺序固定）。
- 当前页持久化：`StoragePreferences["uiState"]="id,index,epoch"`；稳定 ≥30s 且距上次写 ≥120s 才写（`PageHost.cpp:722-766`，常量在 `PageHost.h:107-109`）；恢复优先级 最后停留页(id) > defaultPageId > 第一页（`PageHost.cpp:139-167`），`restoreUiState` 在 `setConfig` 前调用（`mainLogic.cc:100-103`）。
- 中键短按 IP/NO WIFI、长按 3s SoftAP 原样保留（`mainLogic.cc:149-166, 347-377`；覆盖层文案/颜色/时长与 v2.1.2 相同）。
- 旋钮按键 = 立即刷新直播（`mainLogic.cc:191-206` → `PageHost::requestLiveRefresh` → `LiveService::requestRefresh`）；左右键不分配。

### 5.6 LiveService 后台线程/锁/超时/hybrid 路由 ✅

- 单后台线程：`LiveService::start()` detach 1 个 worker（`LiveService.cpp:254-270`），`off`/`push` 不轮询（`LiveService.cpp:587-594`），无网络时跳过且保留 urgent（`LiveService.cpp:595-600`）。
- 调度：`refreshSec` 周期 + urgent 立即醒（`condition_variable::wait_for`，`LiveService.cpp:602-623`）；单飞（同一时间 1 个在途请求，网络 I/O 在锁外，结果回写再取锁；`LiveService.cpp:613-640`）；切页/按键置 urgent 立即刷新；失败不加速重试。
- hybrid 路由：`LIVE_SOURCE_HYBRID && platform==bilibili` → `BilibiliClient::fetchRoom()`（设备 HTTPS 直连），失败且 `live.url` 非空则加入 bridge 批量请求回退；抖音/快手一律进 bridge 批量（`LiveService.cpp:643-751`）。bridge 请求 `GET {url}?rooms=platform:room,...`（`appendRoomsQuery`，`LiveService.cpp:205-232, 697`），HTTP/1.0 + `Connection: close`（`HttpClient.cpp:526-536`），超时 `live.timeoutMs`（`HttpClient.cpp:503-555`），响应 ≤16 KB、支持 chunked/Content-Length。
- 缓存：`LiveCache` 上限 24 条（12 页 × 2 房间，`LiveCache.h/.cpp`），跨页同房间共享；状态机 EMPTY/WAIT/OK/STALE/ERROR，陈旧阈值 `max(3×refreshSec, 60s)`（`LiveCache.cpp:197-215`）；`0` 与"无数据"严格区分（`LiveMetricValue.valid`）。
- 锁顺序：PageHost 锁内不调 LiveService 网络（`PageHost.cpp:570` 在锁外取快照）；LiveService 网络期间不持锁（`LiveService.cpp:625-640`）；LiveService 不回调 PageHost。
- 失败占位：无数据/失败 `--` dim、STALE 旧值 dim（`PageRenderer.cpp:139-167` + `LiveFormat::formatMetric`）。
- bridge 响应解析 `parseLiveBody` 支持契约 §5 全部字段形态（`key` 或 `platform+room`、`ok`/`error`、`metrics` 嵌套或平铺、`title`），缺房间/ok:false → 该格失败/保留旧值（`LiveService.cpp:88-203, 730-750`）；与 `tools/live-bridge/bridge.py` 实际响应（`{ok,ts,rooms:[{key,platform,room,ok,online,likes,fans,status}]}`）字段一致。
- 超时/失败不阻塞 UI：所有网络在后台线程，UI 线程仅 `snapshotForPage` 短锁拷贝（`LiveService.cpp:390-401`）。

### 5.7 B站 TLS 失败不影响 UI ✅（静态）

- B站直连：`BilibiliClient::fetchRoom`（getH5InfoByRoom 主端点 → get_info 兜底，`BilibiliClient.cpp:40-75`）→ `TlsClient::get`：`dlopen("libssl.so.1.1")`/`libcrypto.so.1.1` + 符号表（`TlsClient.cpp:122-190`），CA `/res/ui/cacert.pem`（设备出厂已有 226 KB/145 张根证书，RESEARCH §5.3），`SSL_CTX_set_verify` + `SSL_set1_host` + SNI（`TlsClient.cpp:518-548`），强制 TLS1.2 + 密码套件（`TlsClient.cpp:508-516`），总超时覆盖连接/握手/读写（`TlsClient.cpp:489-491`）。
- **SIGPIPE 防护**：`SigPipeGuard` 在调用线程屏蔽 SIGPIPE 并在退出前回收（`TlsClient.cpp:408-449`），避免对端提前断连（B站 TLS 失败/被 RST）SIGPIPE 杀进程 —— 这是"TLS 失败不影响 UI/不倒计时"的关键加固，已包含在本次构建（19:48:37 版本）。
- 任何 TLS 失败都只在后台线程产生 `ok=false` 的 update → 该格 `--`/旧值 dim，不回调 UI、不阻塞、不写 flash。
- 兜底：hybrid 且配置了 `live.url` 时 B站直连失败会自动回退 bridge（契约 §5）

### 5.8 HttpServer v3 API 与契约 §6 一致性 ✅（1 项 P2 见 §7）

| 契约端点 | 实现 | 结论 |
|---|---|---|
| `GET/POST /api/config` | v3；GET 返回 config + device(+page) + ui + live 摘要；POST 只收 v3、v1/v2 明确 400、保存后热应用（`HttpServer.cpp:349-462`） | ✅（GET 无配置文件时 pages:[] 见 P2-1） |
| `POST /api/page` | `{"id"}`/`{"index"}`（+扩展 `{"reset":true}`）切换当前页，不写配置（`HttpServer.cpp:465-535`） | ✅ |
| `GET /api/live/status` | 返回 ok/source/refresh/timeout/activePage/rooms 明细（状态/指标/年龄/错误，`LiveService.cpp:403-474`） | ⚠ 字段名与契约简表不完全一致（P2-2） |
| `POST /api/live/refresh` | 参数 `page` 可选；只置 urgent + notify，立即返回 `{ok,queued,page}`（`HttpServer.cpp:550-584`） | ✅ |
| `POST /api/live` | push 模式写入缓存；返回 `{ok,updated,ts}`（`HttpServer.cpp:587-607`） | ✅ |
| `GET /api/status` | 增加 `page:{id,index,title,type,count}`，其余 v2 字段保持（`HttpServer.cpp:867-914`） | ✅ |
| `/api/wifi*`、`/api/time` | 保持 v2.1.2（`HttpServer.cpp:609-853, 1046-1052`） | ✅ 未回归 |
| 请求体/上限/错误 | 16 KB 请求体、12 KB 规范化配置、413/400/500 `{ok:false,error}`，CORS/OPTIONS/favicon/404 保持（`HttpServer.cpp:36-37, 202-244, 1015-1068`） | ✅ |

### 5.9 v2.1.2 回归（多 WiFi / SoftAP / 60s NTP / 自定义 NTP / 80→8080 跳转）✅

- 相关源文件**零改动**（`git status` 未列出）：`WifiManager_task.cpp/.h`、`SntpClient.cpp/.h`、`ConfigMode.cpp/.h`、`WifiStore.cpp/.h`、`TimeKeeper.cpp/.h`、`TimeMath.cpp`、`TimerEngine.cpp`、`EventContext.cpp`。
- 60s NTP：`WifiManager_task.cpp:34 kTimeSyncIntervalSec=60`、`:235-242` 每分钟 `SntpClient::syncNow`、`:348` 在线期间每 60s 尝试；`mainLogic.cc:114/237` 仍注入自定义 NTP。
- SoftAP：中键长按 3s 进入/退出、AP 覆盖层文案、`ConfigMode::tick()` 每秒维护均保留（`mainLogic.cc:347-377`, `mainLogic.cc:325`）。
- 多 WiFi：`/api/wifi`、`/api/wifi/list` 处理器与 `WifiStore` 上限 8 保持；主机 `wifi_store` 19 例全绿；网页 WiFi 相关自检 #74–#79 全绿。
- 端口 80→8080 跳转保留（`HttpServer.cpp:156-200, 1129-1134`）。
- 覆盖层/中键行为对照表（DESIGN §3.4）逐项静态核对一致。

## 6. 契约 §8 验收数字

| 验收项 | 要求 | 实测 |
|---|---|---|
| 主机单测 | ≥140 | **209** ✅ |
| 网页自检 | ≥80 | **90/90** ✅ |
| 390px 无横向溢出 / 0 console error | 是 | check_web #5/#8/#88 通过（静态约束 + 沙箱无异常） ✅ |
| 真机 6 项 | 见契约 §8 | 未验证（本窗口禁止部署） ⚠ |

## 7. 问题清单（P0 / P1 / P2 / P3）

### P0（阻断/崩溃/数据损坏/安全）：无

### P1（发布前必须修）：无

### P2（建议修/风险/一致性）

| ID | 问题 | 证据 | 建议 |
|---|---|---|---|
| P2-1 | **GET /api/config 在"无配置文件/文件损坏"时返回 `pages: []`（非法 v3，且与设备运行时默认 4 页不一致）**。`loadConfigOrDefault()` 在 `ConfigStore::load()` 返回 false 后用 `TimerConfig()` 覆盖了 load 已填入的默认 4 页。 | `HttpServer.cpp:279-291`（尤其 `:284 config = TimerConfig();`）；`ConfigStore.cpp:1142-1147`（load 会把 out 置 `defaultConfig()`）；对照 `DESIGN-web-v3.md` §6.2 `:904-906` "无文件时返回默认模板（4 页 + 空目标）"；网页 `normalizeConfig` 对空 pages 会回退默认 4 页（index.html normalizeConfig），故用户侧暂不显性 | 一行修复：`config = ConfigStore::defaultConfig();`，并加主机测试（HttpServer 层无法主机跑时可测 `loadConfigOrDefault` 抽出的纯逻辑/或记录到真机清单） |
| P2-2 | `/api/live/status` 应答字段与 `V3-CONTRACT.md` §6 简表不完全一致：无顶层 `ts`/`lastOk`/`lastError`，改为 `source.lastOkUtc`/`source.error`/`source.reachable`，`rooms[]` 为每房间明细（page/slot/state/metrics/ageSec…）。网页未消费该接口（`web-src/index.html` 无 `/api/live/status` 引用），影响面为外部脚本/契约文档 | `V3-CONTRACT.md:142`；`LiveService.cpp:403-474`；`grep -n "api/live" web-src/index.html` 仅命中 push 文案 | 二选一：更新契约 §6 为实际形状（推荐，信息更全）；或实现补顶层别名。与 docs-v3 任务联动 |
| P2-3 | hybrid 下 B 站直连未使用 `live.timeoutMs`：`BilibiliClient::fetchRoom` 固定 4000 ms/请求，主端点失败还会再请求兜底 → 单房间最坏 ~8 s，一页 2 个 B 站房间最坏 ~16 s（仍 < 默认 refresh 30 s，但偏离契约 §2 `timeoutMs` 语义） | `LiveService.cpp:655-668`（未传 timeout）；`BilibiliClient.h:23` 默认 4000；`BilibiliClient.cpp:52-66` 主+兜底两次请求 | 把 `mSource.timeoutMs` 传入 `BilibiliClient::fetchRoom`（接口已留默认参数），或明确文档写"B 站直连超时独立 4 s/端点" |
| P2-4 | TlsClient **每次请求新建 SSL_CTX 并加载 226 KB CA**（`SSL_CTX_load_verify_locations`），与 `RESEARCH-live-apis.md` §5.7（`:555-560`）"全局只建一个 SSL_CTX、CA 只加载一次"的建议不一致；hybrid 下 B 站 2 房间/30 s → 2 次 CA 解析 | `TlsClient.cpp:500-524`；RESEARCH §5.7 | 功能正确、35 MB 设备周期低，建议真机观察 RSS/首帧延迟；若偏紧，下一版改全局 SSL_CTX + CA 单次加载 |
| P2-5 | `LiveCache` 与 bridge/push 响应解析（`LiveService.cpp::parseLiveBody`）**无主机单测**：LiveCache 为纯 C++ 却未编入 `tests/run_tests.ps1`；bridge 解析逻辑在 device-only 的 LiveService.cpp 内。STALE 阈值/缺项/0 值语义仅靠静态审查 | `tests/run_tests.ps1` appSources 列表（无 `live/LiveCache.cpp`）；`LiveService.cpp:88-203` | 建议补 `tests/test_live_cache.cpp`（纯 C++ 可测）并把 bridge 响应解析抽到可测 layer（后续版本）；本次以静态审查 + bridge 侧 40 测试兜底 |
| P2-6 | **直播空槽渲染固件 vs 网页模拟器不一致**：固件 `isCellConfigured = enabled && room非空`，只有"整页无已配置房间"才 `NO LIVE`，部分空槽留黑；网页模拟器只按 `enabled` 过滤，启用但空房号显示 `label + --`，只有全停用才 NO LIVE。默认 4 页配置的 p3 空 live 页：固件=NO LIVE，网页=B --/D -- | `PageRenderer.cpp:126-145`；`LiveFormat.cpp:200-202`；`web-src/index.html` 直播帧 NO LIVE 逻辑（`enabledRooms` 过滤）；check_web #20/#21 明确断言网页行为 | 契约 §3 "未配置 `--` dim / 整页无数据 `NO LIVE`" 表述可两解：建议明确"空槽=留黑还是 --"，并让模拟器与固件一致（建议固件按契约显示 `--` dim，或网页改留黑），同时补文档 |
| P2-7 | 旋钮软件消抖 120 ms（`mainLogic.cc:29, 173-177`）与 `DESIGN-pages-v3.md` §3.3 `:613` "旋钮不做软件消抖、快速连转连续翻页" 有偏差：快速旋转（<120 ms/格）可能丢格 | `mainLogic.cc:29 kKnobDebounceMs=120`、`:173-178`；DESIGN-pages-v3 §3.3 | 真机验证手感；若丢格明显，移除或降到 ~30 ms（KeyManager 已是"起点+终点"配对，一格一事件） |
| P2-8 | DNS 解析无超时：`HttpClient`/`TlsClient` 的 `getaddrinfo` 为阻塞调用，坏 DNS 时后台线程可能阻塞超过 `live.timeoutMs`（不阻塞 UI，但轮询周期漂移/延迟变大） | `HttpClient.cpp::connectTcp`（getaddrinfo）；`TlsClient.cpp::connectTcp`（getaddrinfo） | 低风险；可在下一版用 `getaddrinfo_a`/线程化解析或接受现状并记入文档 |
| P2-9 | `/api/status` 在 **live 页稳态**时 `state.nowUtc` 等字段可能滞后：live 页脏帧签名不含 `nowUtc`，`updateStatusLocked` 只在脏帧执行 | `PageHost.cpp:656-660`（非 live 才混入 nowUtc）、`:691-699`（仅脏帧更新 status） | 影响网页/脚本的诊断精度（时钟/倒计时页每秒脏帧无此问题）；建议 status 的 nowUtc 每 tick 单独更新，或在契约里说明 |
| P2-10 | 设计文档与冻结契约冲突，后续易误读：`DESIGN-pages-v3.md` 仍写"一页 4 个直播间/2×2"（12 处）、`DESIGN-web-v3.md` 仍写 4 行/2×2（45 处），实现按契约 2 槽；`PageTypes.h:107/294` 注释也残留 "1..4"/"2x2" | `docs/DESIGN-pages-v3.md`、`docs/DESIGN-web-v3.md`（本轮未改）；`native/MultiTimer/src/pages/PageTypes.h:107,294` | 建议 docs-v3 在文档顶部加"以 V3-CONTRACT 为准：live 页 2 行/2 房间"标注并清理相关段落；代码注释顺手改（非功能） |

### P3（记录项，可不修）

- `/api/page` 支持契约外的 `{"reset":true}`（附加扩展，不破坏契约）；`goToPage` 不过滤 `enabled`（"跳过 disabled"只约束旋钮，HTTP 显式跳转合理）。
- `device.page.index` 与 `POST /api/config`/`/api/page` 响应中的 `index` 为 0-based，`DESIGN-web-v3.md` §6.2 写 1-based；网页只用 `device.page.id`，不消费 index（文档/契约建议统一为 0-based 并注明）。
- `PageHost::persistTick` 在 UI tick 内同步写 `StoragePreferences`（30 s/120 s 节流；与 v2 `TimeKeeper` 写 flash 模式一致，最坏阻塞 UI 数十 ms，观察项）。
- 覆盖层 `mBannerIndex` 到期不显式清零（靠时间判断），无功能影响。

## 8. 未验证点（真机/部署，需 20:35 后 /tmp 联调）

1. **250 ms 定时器**在 FlyThings 上的实际最小粒度与横幅（1.2 s）可见性；若被 SDK clamp，需退 500 ms（设计已列风险）。
2. **旋钮 CW/CCW** 事件经 KeyManager 是否一格一事件、120 ms 消抖手感；中键短按 IP/长按 SoftAP 在真机复测。
3. **分屏页**上下同一秒 / 跳时 / 改时区立即变化（主机像素测试已覆盖，真机看 LED）。
4. **live 页** TomThumb 两行几何/平台色/`--`/STALE dim/NO LIVE 真机观感（并确认 P2-6 与模拟器差异的最终取向）。
5. **B站直连 TLS**：设备 `dlopen libssl/libcrypto 1.1`、`/res/ui/cacert.pem`、强制 TLS1.2/密码套件、SIGPIPE 防护、真实握手耗时与失败降级；B站真实房间数据（人气/点赞/粉丝/开播）。
6. **hybrid 路由**：设备 ↔ bridge（`tools/live-bridge`）真实请求/响应、B站直连失败回退 bridge、抖音/快手经 bridge、push 模式 `POST /api/live`；`live.refreshSec` 30 s 轮询与切页/旋钮立即刷新。
7. **v2.1.2 配置在设备上的迁移**：v2 `/data/multitimer.conf` 升级后 targets/tz/colors/ntp/页面保留（WiFi 列表在独立存储）并写回 v3 一次。
8. **HTTP v3 API 真机**：手机浏览器 `GET /`、`GET/POST /api/config`、`POST /api/page`、`GET /api/live/status`、`POST /api/live/refresh`、`POST /api/live`；400/413/404 错误形态；`GET /api/config` 在无配置文件设备上的 P2-1 行为。
9. **uiState 当前页持久化**：稳定 30 s/间隔 120 s 落盘、重启恢复优先级、`/api/page {"reset":true}`。
10. **v2.1.2 不回归真机项**：多 WiFi 保存/切换/重连、SoftAP 进入退出、60 s NTP、自定义 NTP、80→8080 跳转。
11. **资源**：zkgui RSS/可用内存（TLS + live 线程 + 页面渲染）、SPI 帧率/时延、flash 写入频率。
12. 本窗口**未做任何部署/刷机/重启**；`build/v3-dev-workspace` 当前产物仍是 TLS 加固前版本（714,340 B），真机联调请用本报告 §2 的最终产物或按冻结源码重编。

## 9. 复现命令汇总

```powershell
# 1) 构建（独立 workspace + 日志；要求 0 errors）
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 `
  -Workspace build\qa-v3-workspace -LogPath build\qa-v3-build.log
# 产物：build\qa-v3-workspace\MultiTimer\Release\libzkgui.so

# 2) 主机单测（要求全绿；本轮 209/209）
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1

# 3) 网页自检（要求全绿；本轮 90/90）
node native\MultiTimer\tools\check_web.js

# 4) 关键静态检查（PowerShell 等价）
Select-String -Path native\MultiTimer\src\pages\PageHost.cpp -Pattern 'time\(NULL\)'
Select-String -Path native\MultiTimer\src -Recurse -Pattern 'remainSeconds\s*--|mLastRenderSec|cachedRemain'
Select-String -Path native\MultiTimer\src\net\HttpServer.cpp -Pattern 'kBodyMaxBytes|kConfigMaxBytes'
Select-String -Path native\MultiTimer\src\live\LiveService.cpp -Pattern 'LIVE_SOURCE_HYBRID|BilibiliClient::fetchRoom'
```

产物/日志哈希：

| 文件 | 大小 | SHA256 |
|---|---|---|
| `build/qa-v3-workspace/MultiTimer/Release/libzkgui.so` | 718,436 B | `E92CAFD058F9B67A33A2D223178852036E8C3CCCF1FC1A9B84B26EDE616F6443` |
| `tests/out/multitimer_tests.exe` | 1,956,864 B | `D6916FB2B4630CBABA0042FB68A0A719ED368DEE4ADACAD430E1C6BF53B21815` |
| `build/qa-v3-build.log` | 16,373 B | `CCDEDEA9EB2DE1D0E6870A293378C9274FFA00EE31F501BB313B294A8A934089` |
| `build/qa-v3-webcheck.log` | — | `DB0B2DCEEB5CDC8CD71E43D0F9A6BFE913F5E2454C6B0D8D2CD1FAF415CE7104` |
| 源码清单（73 文件） | — | `AC0B667100342FB6976B54C7A31280958F5E910F2C54C009F82EDA051B2CE915` |

## 10. 限制声明

- 本报告只做**主机侧**构建/测试/静态审查；未修改任何源码、未改设备、未部署/刷机/重启（遵守 20:35 前禁令）。
- 除本报告外，仅由要求的构建/测试命令生成 `build/qa-v3-*` 日志与产物、`tests/out` 编译产物。
- 设备真机项与 §8 清单未验证；P2-1/P2-2 为 API 契约一致性建议，不阻断 20:35 后的 /tmp 真机联调（联调时请特别观察 P2-6 的渲染取向与 P2-3 的超时表现）。
