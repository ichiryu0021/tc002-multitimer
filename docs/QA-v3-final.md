# TC002 MultiTimer v3 — 最终回归报告（task-4）

- 执行者：qa-v3
- 执行时间：2026-10-22 20:00:08 – 20:03（Asia/Shanghai），主机侧
- 范围：v3 最终源码（含 Lead 对 QA P2-1/P2-3 的修复）独立重编 + 单测 + 网页自检 + 修复复核 + 最终 hash 确认
- 前置：第一轮 QA 报告见 [QA-v3-initial.md](QA-v3-initial.md)（基线产物 `E92CAFD0…`，19:51:56，修复前）
- 禁止项遵守：**未部署、未刷机、未重启设备**；只读源码，本报告为唯一新增产物（另有 QA 构建/测试日志）

## 0. 结论摘要

| 项目 | 结果 |
|---|---|
| 独立重编（QA workspace） | ✅ `0 errors, 0 warnings`；`libzkgui.so` 718,436 B |
| 最终 SHA256 | ✅ **`A4B1D7E8FAB7803776A1C410E5F5128C28B3354541DD272BA1AAE1C500E4B99F`** |
| 与 Lead 产物一致性 | ✅ **与 Lead `build/v3-final-workspace`（19:57:11）逐字节一致**（同一 hash）→ 构建可复现 |
| 主机单测 | ✅ 209/209 passed（0 failed，exit 0） |
| 网页自检 | ✅ 90/90 通过（exit 0）；`editor_gz.h` 与 `index.html` gzip 逐字节一致 |
| P0 / P1 | **0 / 0** |
| P2 修复复核 | ✅ **P2-1 已修**；✅ **P2-3 已修**；其余 8 项仍为 open（与上轮一致），新增 2 项 P2（契约内部不一致、60 分钟退避泛化） |
| 设备 B站 TLS | ⚠ 设备实测不可用（`SSL_connect` 返回 `141640BF no protocols available`）；**不是代码回归**，TLS 直连保留为未来固件路径，已写入契约/文档/网页文案 |
| 真机验收 | ⚠ 未执行（20:35 前禁止部署；待验证清单见 §8） |

一句话：**最终源码在独立 workspace 重编后与 Lead 产物逐字节相同（718,436 B / `A4B1D7E8…`），209/209 单测、90/90 网页自检全绿；P2-1/P2-3 修复经代码复核与网页文案/契约同步验证；B站 TLS 不可用是设备环境事实、非回归。可进入 20:35 后 /tmp 真机联调。**

## 1. 最终源码快照（冻结）

- 工作区 HEAD：`b2a4b2706a9b93668e9d3a892e00a372c8bbea8e`（v3 变更未提交；`git status` 40 项）
- 源码冻结时间：**2026-10-22 19:57:40**；19:57:40 之后至 20:03 复查无任何源码变化（不含文档 .md 与生成物 `editor_gz.h`）
- 本轮相对上一轮 QA 基线的**全部源码变化**（逐文件比对 `build/qa-v3-workspace` 快照）只有 4 处，均为 Lead 声明的 P2 修复/配套改动：
  1. `src/net/HttpServer.cpp` 19:56:24（P2-1：`loadConfigOrDefault` 改 `ConfigStore::defaultConfig()`）
  2. `web-src/index.html` 19:56:50（直播源文案：B站直连当前固件不可用）
  3. `src/live/LiveService.cpp` 19:57:00（P2-3：hybrid 路由 + 60 分钟退避 + 传 `timeoutMs`）
  4. `src/web/editor_gz.h` 19:57:39（由新 index.html 重新生成；gzip 内容一致性已校验）
  其余源文件（PageHost/PageRenderer/ConfigStore/TlsClient/BilibiliClient/mainLogic 等）与 QA 基线逐字节相同。
- 源码清单哈希（`native/MultiTimer/src` 73 文件，按路径排序后逐文件 SHA256 再整体 SHA256）：`91BAFA21C0CB545BFEE7E128FD17E7C4A3892B8935D69317C0143BB05E8D19BB`

关键文件 SHA256：

| 文件 | SHA256 |
|---|---|
| `src/net/HttpServer.cpp` | `98C74B0183F917F682ACC1FC0AADB8DDC464D87CEC29EA80098EF107F2D2C702` |
| `src/live/LiveService.cpp` | `0AD11164841C11E6BFAD0F13BAAA67B08E543C42FDED8FEDE0241744240DA3A3` |
| `src/web/editor_gz.h` | `E85D97EC0639AF2CC74947029A8BAAE524F70E0EF47DB706DA14223A88DC3B3E` |
| `web-src/index.html` | `361A07A1976A94D4594D75D5B498350E9CBF47AA42CA8697EEB212DCF0EE4152` |
| `tools/check_web.js` | `597E16C3D6D65CA98AE44A330FA7AF3C8C57464E02429DC22EF661A81D9605E3` |
| `docs/V3-CONTRACT.md` | `4312F3D1D4138ADE63FCE34EE26D400437E1EC09BA811CE6D30742CF54752ADB` |
| 源码清单（73 文件） | `91BAFA21C0CB545BFEE7E128FD17E7C4A3892B8935D69317C0143BB05E8D19BB` |

## 2. 构建验证（独立重编 + hash 确认）

命令（独立 workspace/日志，不与 Lead 复用）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 `
  -Workspace build\qa-v3-final-workspace -LogPath build\qa-v3-final-build.log
```

结果（20:00:08 → 20:00:19，20.3 s）：

- IDE exit code `0`；日志末行 `Build Finished. 0 errors, 0 warnings. (took 5s.342ms)`；`Linking libzkgui.so` 成功
- 产物：`build/qa-v3-final-workspace/MultiTimer/Release/libzkgui.so`
  - 大小 **718,436 B**
  - SHA256 **`A4B1D7E8FAB7803776A1C410E5F5128C28B3354541DD272BA1AAE1C500E4B99F`**
- 构建日志：`build/qa-v3-final-build.log`，16,373 B，SHA256 `CEDD0660EB6A25F0A694D1E99E6CCFA8330F068C16E0243187FFF7451C5A4925`；`error:` 0 行、`warning:` 0 行（GCC `note: parameter passing …` 为 ABI note，不计 warning）
- **可复现性确认**：QA 独立重编（20:00:18）与 Lead `build/v3-final-workspace`（19:57:11）产物 **大小与 SHA256 完全一致**（`A4B1D7E8…`）；`build/v3-tmp-deploy/lib/libzkgui.so` 亦为同一 hash。构建输入快照比对：QA workspace 与当前源码 87 个文件中仅 IDE 自行改写的 `.settings/language.settings.xml` 不同，源码/网页资源全部一致。
- 历史产物（仅作参照，勿用于最终联调）：`E92CAFD0…F6443`（上一轮 QA，19:51:56，P2 修复前）、`2418B29…F1E9F4` / 714,340 B（Lead v3-dev workspace，19:41:13，TLS 加固前）。

## 3. 主机单测

命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1
```

结果（MSVC 14.44 / `std:c++14`，从源码重新编译）：

```text
Summary: 209 cases | passed 209 | failed 0
RESULT: ALL TESTS PASSED
[run] exit code: 0 (0 = all cases passed)
```

- 分组：bilibili 17、config 54、engine 19、json 10、live_format 24、renderer 42、timemath 24、wifi 19 = **209**
- `tests/out/multitimer_tests.exe`：1,956,864 B，SHA256 `4DA7DD0F1204D4DC6AB859D1E0C4B75721713928477E576888A6C8AB2D9BBF2E`（PE 含时间戳，不同次编译 hash 会变，仅记录本次）
- 日志：`build/qa-v3-final-tests.log`，SHA256 `4677E6051258C3ACA5A350E886C7142AAB578D008D7D11FFCC7B434AF0ABE5AF`
- 本轮源码改动均属 HTTP/LiveService 平台层，不进入主机单测闭环；209 用例与上轮完全一致，v2→v3 迁移、页面像素、LIVE 格式、v2.1.2 回归（wifi/engine/time_math/json）全绿。

## 4. 网页自检

命令：

```powershell
node native\MultiTimer\tools\check_web.js
```

结果（20:00:08，exit 0）：

```text
PASS 1..90
----------------------------------------------------------
90/90 通过
```

- 日志：`build/qa-v3-final-webcheck.log`，SHA256 `DB0B2DCEEB5CDC8CD71E43D0F9A6BFE913F5E2454C6B0D8D2CD1FAF415CE7104`
- `check_web.js` 未改动（19:37:03，90 个编号检查）；index.html 的直播源文案改动后 90/90 仍全绿（含 #50 数据源 UI、#51 URL 校验等）
- 内嵌资源一致性：`web-src/index.html`（181,834 B / `361A07A1…`）重新 gzip（mtime=0, level 9）与 `src/web/editor_gz.h` **逐字节一致**（gzip 45,595 B，`MATCH True`）→ 最终固件 `GET /` 返回的就是自检通过的这版页面。

## 5. Lead 修复复核

### 5.1 P2-1（GET /api/config 无配置/损坏时返回非法 pages:[]）✅ 已修

- 当前 `HttpServer.cpp:279-290`：`ConfigStore::load()` 失败时 `config = ConfigStore::defaultConfig();`（注释："磁盘无配置/损坏时返回默认 v3 页表（与 mainLogic 启动兜底一致）"）。
- 与 `ConfigStore::defaultConfig()`（4 页：p0 clock / p1 clock_countdown / p2 countdown / p3 live 2 空槽、source=off、defaultPageId=p0）一致，也与 `mainLogic` 启动兜底一致；`handleGetConfig`/`handlePostTime`/`handleGetStatus` 的该 helper 均受益。
- 验证方式：代码静态复核 + `ConfigStore::defaultConfig()` 有主机测试（`config_default_config_has_four_pages`、`config_load_missing_file_returns_default` 通过）。HttpServer 为设备平台层无主机测试，**真机验证项见 §8-10**（无配置文件设备 `GET /api/config` 应返回 4 页合法 v3）。

### 5.2 P2-3（hybrid 路由/超时/退避）✅ 已修，行为变化已同步契约与网页文案

- 60 分钟退避：`LiveService.cpp:61-83`，`kBilibiliDirectBackoffMs = 60*60*1000`，`bilibiliDirectSuspended()/suspendBilibiliDirect()/clearBilibiliDirectSuspension()` 全局原子状态；避免每个房间白等 2×timeout。
- hybrid 路由（`LiveService.cpp:680-723`）：hybrid 且 `live.url` 非空 → **B站也走 bridge**（不尝试直连）；`live.url` 为空 → B站尝试设备直连（超时后再退避）；直连成功清除退避，失败置 60 分钟退避并给出错误串；抖音/快手始终走 bridge。
- 超时：`LiveService.cpp:590-591` 按 `live.timeoutMs` clamp 后经 `fetchPage(..., timeoutMs, ...)`（`:652`）传入 `BilibiliClient::fetchRoom(key.room, data, timeoutMs)`（`:693-694`）；bridge 请求同样使用该 timeout（`:738`）。
- 配套同步：
  - `docs/V3-CONTRACT.md`（19:57:41）已更新 §1 目标（`SSL_connect 141640BF`、B站直连为未来路径、配 bridge 三平台都走 bridge）、§2 `live.source` 行（hybrid 配 url 时三平台都走 bridge；url 为空 B站才直连）、§5 bridge 协议（B站直连路径保留但不可用）。
  - 网页文案（index.html:549/569/738/3513/3515）已改为"B站直连当前固件不可用/配置中继后 B 站也走中继"，并重新生成 `editor_gz.h`（gzip 一致性校验通过）。
  - 网页自检 90/90 全绿。
- 结论：修复符合 task-4 描述的预期；**行为变化（hybrid+url 时 B站不再直连）属有意变更**，已不是我上一轮报告中的"契约偏离"，但见 §7 P2-11（契约 §4 残留旧描述）与 P2-12（退避泛化风险）。

## 6. 设备 B站 TLS 不可用（环境事实，非代码回归）

- 设备实测：`/lib/libssl.so.1.1` 的 TLS client **无可用协议**，`SSL_connect` 返回 **`141640BF`（no protocols available）**，对所有 HTTPS 站点；主机侧用设备 `libssl.so.1.1` 亦复现（raw server 收到 0 字节）。
- 已在以下位置作为事实记录（本轮未改动设备）：
  - `docs/V3-CONTRACT.md` §1（`SSL_connect 返回 141640BF no protocols available` → B站直连仅保留为未来路径；配 bridge 时三平台走 bridge）与 §5。
  - `docs/STATE-2026-10-22-v3.md`（20:00:46）多处：`141640BF`、所有 HTTPS 不可握手、TLS 直连为未来固件保留。
  - `docs/HANDOFF-native.md`（20:00:13）同样记录。
  - 网页文案已明确提示用户（见 §5.2）。
- 最终固件仍包含完整 TLS 加固（`dlopen libssl/libcrypto`、CA `/res/ui/cacert.pem`、强制 TLS1.2/密码套件、**SIGPIPE 防护**、主机名校验），作为**未来更换带 TLS 的固件后启用**的路径；当前固件上该路径不可用**不是本次代码引入的回归**，也不会阻塞 UI/倒计时（后台线程失败 → 直播格 `--`/旧值 dim；hybrid 配 bridge 时三平台全部走 bridge 正常工作）。
- 真机复测建议：部署后 `GET /api/live/status` 观察 `source.kind` 与 B站行错误文本；预期 B站直连仍不可用（若仅配 bridge 则 B站经 bridge 正常）。

## 7. P2 状态（上轮 → 本轮）

| ID | 上轮问题 | 本轮状态 | 说明/证据 |
|---|---|---|---|
| P2-1 | GET /api/config 无配置/损坏时 pages:[] | ✅ **已修** | `HttpServer.cpp:279-290` 用 `ConfigStore::defaultConfig()`；静态复核通过，真机待验（§8-10） |
| P2-3 | hybrid B站直连未用 timeoutMs/路由语义 | ✅ **已修**（按新契约语义） | `LiveService.cpp:61-83, 590-591, 652, 680-723, 693-694`；契约 §1/§2/§5 与 web 文案已同步；web 90/90 |
| P2-2 | `/api/live/status` 应答字段与契约 §6 简表不一致（无顶层 ts/lastOk/lastError；source 内嵌） | ⏳ open | `LiveService.cpp:403-474` 未变；契约 §6 line 144 仍写简表；网页未消费该接口。建议契约按实际形状更新 |
| P2-4 | TlsClient 每请求新建 SSL_CTX + 加载 226 KB CA | ⏳ open（降级为观察项） | `TlsClient.cpp:500-524` 未变；当前固件 TLS 路径不可用，实际不会触发；未来 TLS 固件再优化 |
| P2-5 | LiveCache/bridge 解析无主机单测 | ⏳ open | `tests/run_tests.ps1` appSources 未含 `LiveCache.cpp`；`parseLiveBody` 在 device-only 的 LiveService.cpp |
| P2-6 | 直播空槽渲染固件（NO LIVE/留黑）与网页模拟器（label+--）不一致 | ⏳ open | `PageRenderer.cpp:126-145`、`LiveFormat.cpp:200-202`、`index.html` 直播帧逻辑未变；check_web #20/#21 仍断言网页行为；契约 §3 "未配置 -- / 整页无数据 NO LIVE" 仍可两解 |
| P2-7 | 旋钮 120 ms 软件消抖 vs 设计"不消抖" | ⏳ open | `mainLogic.cc:29,173-177` 未变；真机手感待验证 |
| P2-8 | DNS 解析无超时（HttpClient/TlsClient getaddrinfo） | ⏳ open | 低风险；网络异步不影响 UI |
| P2-9 | live 页稳态 `/api/status.nowUtc` 滞后 | ⏳ open | `PageHost.cpp:656-660, 691-699` 未变；诊断精度问题 |
| P2-10 | 设计文档仍写"一页 4 间/2×2"；PageTypes 注释残留 | ⏳ open（部分） | `docs/DESIGN-pages-v3.md`/`DESIGN-web-v3.md` 未改（契约已更新，实现 2 槽）；`PageTypes.h:107,294` 注释仍在 |
| **P2-11（新）** | 契约 §4 line 107-108 仍写"B站房间走 HTTPS 直连（每房间独立请求），抖音/快手按页批量走 bridge HTTP"；§2 `live.url` 行仍写"用于抖音/快手"——与更新后的 §1/§2 line 56/§5 及实现（hybrid+url 时 B站也走 bridge）冲突 | ⏳ open | `docs/V3-CONTRACT.md:107-108, 57`；建议把 §4 该条改为"B站直连路径保留但当前固件不可用；hybrid 配 url 时三平台都走 bridge" |
| **P2-12（新）** | B站直连 60 分钟退避对**所有**直连失败类型生效（含普通网络/超时失败）。当前固件 TLS 恒失败无影响；未来 TLS 可用固件上一次瞬时失败会导致 60 分钟不再直连 | ⏳ open | `LiveService.cpp:76-83, 711`；建议只对 TLS 不可用/协议类错误退避，或指数退避 |

P3 记录：60 分钟退避为进程内全局状态、配置热替换不清除（url 非空即不再触发直连，影响小）；`/api/page` 的 `{"reset":true}` 为契约外附加扩展；`device.page.index` 为 0-based 而 DESIGN-web §6.2 写 1-based（网页只消费 id）。

## 8. 未验证点（真机，20:35 后才能做）

本窗口**未部署/未刷机/未重启设备**（设备 19:30/20:00/20:30 目标倒计时 + 20:35 前禁令）。以下待 20:35 后 /tmp 联调：

1. **部署**：用 `build/v3-final-workspace`（或 `v3-tmp-deploy`，已含同一 hash 的 `.so`）部署；确认 0/0 产物在设备正常启动（防砖属性/`sys.zkapp.state` 不变）。
2. **B站 TLS 事实复测**：hybrid+bridge 下三平台（B站/抖音/快手）经 bridge 出数；预期 B站直连仍 `141640BF` 不可用（非回归），直连路径不阻塞 UI。
3. **hybrid 路由**：`live.url` 非空 → B站走 bridge；清空 url → B站尝试直连并进入 60 分钟退避（`/api/live/status` 可观察错误/状态变化）；bridge 轮询 30s、切页/旋钮立即刷新。
4. **v2→v3 配置迁移**：设备原 v2 `/data/multitimer.conf` 升级后 targets/tz/colors/ntp/pages 保留并写回 v3 一次；WiFi 列表（独立存储）保留。
5. **页面/旋钮/持久化**：4 页渲染、旋钮切页跳过 disabled、横幅 1.2s、覆盖层优先级、uiState 30s/120s 节流与重启恢复。
6. **250 ms 节拍**：FlyThings 定时器实际最小粒度、稳态约 1 帧/秒、SPI/UI 时延。
7. **HTTP v3 API**：手机浏览器 GET/POST `/api/config`、`POST /api/page`、`GET /api/live/status`、`POST /api/live/refresh`、`POST /api/live`；P2-1 场景（无配置文件设备 GET /api/config 应返回默认 4 页）；P2-2 字段形状按实际消费方确认。
8. **v2.1.2 回归真机**：多 WiFi 保存/切换/重连、SoftAP 进出、60s NTP、自定义 NTP、80→8080 跳转。
9. **资源**：RSS/可用内存、TLS/live 线程、flash 写入频率。
10. P2-6 空槽渲染、P2-7 旋钮消抖手感的最终产品取向（固件或网页模拟器统一）。

## 9. 复现命令与哈希

```powershell
# 独立重编（要求 0 errors/0 warnings）
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 `
  -Workspace build\qa-v3-final-workspace -LogPath build\qa-v3-final-build.log
# 产物：build\qa-v3-final-workspace\MultiTimer\Release\libzkgui.so

# 主机单测（本轮 209/209）
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1

# 网页自检（本轮 90/90）
node native\MultiTimer\tools\check_web.js
```

| 文件 | 大小 | SHA256 |
|---|---|---|
| `build/qa-v3-final-workspace/MultiTimer/Release/libzkgui.so` | 718,436 B | **`A4B1D7E8FAB7803776A1C410E5F5128C28B3354541DD272BA1AAE1C500E4B99F`** |
| `build/v3-final-workspace/MultiTimer/Release/libzkgui.so`（Lead） | 718,436 B | `A4B1D7E8FAB7803776A1C410E5F5128C28B3354541DD272BA1AAE1C500E4B99F`（与 QA 一致） |
| `build/v3-tmp-deploy/lib/libzkgui.so`（部署暂存，未部署） | 718,436 B | `A4B1D7E8FAB7803776A1C410E5F5128C28B3354541DD272BA1AAE1C500E4B99F` |
| `tests/out/multitimer_tests.exe` | 1,956,864 B | `4DA7DD0F1204D4DC6AB859D1E0C4B75721713928477E576888A6C8AB2D9BBF2E` |
| `build/qa-v3-final-build.log` | 16,373 B | `CEDD0660EB6A25F0A694D1E99E6CCFA8330F068C16E0243187FFF7451C5A4925` |
| `build/qa-v3-final-tests.log` | 19,214 B | `4677E6051258C3ACA5A350E886C7142AAB578D008D7D11FFCC7B434AF0ABE5AF` |
| `build/qa-v3-final-webcheck.log` | 8,616 B | `DB0B2DCEEB5CDC8CD71E43D0F9A6BFE913F5E2454C6B0D8D2CD1FAF415CE7104` |
| 源码清单（73 文件） | — | `91BAFA21C0CB545BFEE7E128FD17E7C4A3892B8935D69317C0143BB05E8D19BB` |
| 上一轮 QA 基线产物（历史） | 718,436 B | `E92CAFD058F9B67A33A2D223178852036E8C3CCCF1FC1A9B84B26EDE616F6443`（19:51:56，P2 修复前） |

## 10. 限制声明

- 只做主机侧验证：未修改任何源码、未部署/刷机/重启设备；除本报告外仅由构建/测试命令生成 `build/qa-v3-final-*` 日志与 `build/qa-v3-final-workspace`、`tests/out` 编译产物。
- 真机项与 §8 清单未验证；设备 B站 TLS 不可用结论来自设备实测（`141640BF`）与已更新文档，不是本次代码回归。
- 部署暂存目录 `build/v3-tmp-deploy/` 已含最终 `.so`（hash 与 QA/Lead 构建一致），但**未执行任何部署动作**；20:35 后由 Lead/部署负责人执行。
