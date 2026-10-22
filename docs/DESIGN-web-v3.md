# TC002 MultiTimer v3 网页配置器设计（可编辑页面系统 + 直播监控）

> 状态：**设计稿，待评审**。本文件只描述契约与交互，不改源码/页面/设备。
> 上游依据：`docs/DESIGN-v2.md`、`native/MultiTimer/web-src/index.html`、
> `native/MultiTimer/tools/check_web.js`、`native/MultiTimer/src/{pages,config,timer,net}/*`、
> `native/MultiTimer/src/Fonts/TomThumb.h`、`docs/STATE-2026-10-22-v2.md`、
> `docs/HANDOFF-native.md`、`native/MultiTimer/README.md`。
> 参考实现核对时间：2026-10-22 19:00 前。**注意**：核对期间工作树出现并行源码改动
> （`ConfigStore.cpp`、`HttpServer.cpp`、`TimerPage.cpp`、`web-src/index.html` 等，
> 时间戳 18:52–19:05），本文以 `DESIGN-v2.md` 冻结契约与 v2 行为为基线；
> 实现前必须与最新代码/已冻结 schema 复核，不能把本文当成“当前代码快照”。
> 并行设计输入：`docs/DESIGN-pages-v3.md`（固件/显示架构）、`docs/RESEARCH-live-apis.md`
> （平台接口实测）。本文是**网页配置器/模拟器**视角；三份文档的差异与合并建议见 §12，
> 实现前必须先冻结一份统一 schema。
>
> 一句话目标：在**不破坏 v2 时钟/倒计时/时区/颜色/WiFi/多地址**的前提下，把单屏升级为
> **可编辑的多页面系统**（旋钮切页、页面增删排序改名选类型、默认页/当前页），并增加
> **直播页**（每页 4 个直播间、平台/房间号/指标）与**网页侧直播监控**；52×16 模拟器能
> 预览全部页面类型与直播占位状态。

---

## 0. 范围、术语与非目标

### 0.1 范围

| 范围 | 内容 |
|---|---|
| 配置 | v3 JSON schema、页面模型、直播行模型、上限、v2/v1 迁移 |
| 网页 | 页面列表/编辑器/直播行/直播监控/保存校验/未保存提示/移动端 |
| 模拟器 | 按选中页面预览时钟、分屏、倒计时、直播 2×2；加载/空/错误/离线占位 |
| 固件契约 | `GET/POST /api/config` v3、`POST /api/page`、`GET /api/live/status`、`/api/status` 扩展 |
| 测试 | `check_web.js` 新增测试点清单；固件主机单测建议 |
| 实施 | 分阶段建议与依赖（尤其直播数据接口） |

### 0.2 术语

| 术语 | 含义 |
|---|---|
| 页面（page） | 一个可命名、可排序的显示单元；类型为 `clock` / `split` / `countdown` / `live` |
| 默认页 | 设备**开机**显示的页面，存在配置的 `defaultPageId` |
| 当前页 | 设备**运行中**显示的页面；由旋钮/网页切换，不写入配置 |
| 直播行 | 直播页 4 个槽位之一：平台 + 房间号 + 启用 + 屏幕主指标 |
| 屏幕主指标 | 该直播行在 52×16 上显示的**一个**指标；网页监控始终显示该房间全部可用指标 |
| 帧模型 | 固件与模拟器共享的“文本 run + 直播单元”描述，渲染前先生成、可测试 |
| 能力表 | 固件实际支持的平台→指标集合，由 `GET /api/live/status` 上报 |

### 0.3 明确不做（v3.0）

- 不做拖拽排序（用上移/下移按钮，移动端更稳）。
- 不做每页独立配色/主题；沿用 v2 的三个全局颜色。
- 不做直播指标轮播、不做多指标拼屏（屏幕主指标只选一个；网页显示全部）。
- 不做中文字库/中文点阵（LED 仍只支持 TomThumb 可打印 ASCII 子集）。
- 不做直播数据持久化、不做历史曲线、不做开播提醒/推送。
- 不做网页直连 B 站/抖音/快手 API（CORS、反爬、零外链三重约束）。
- 不做**配置 HTTP 服务**的鉴权/TLS/OTA；HTTP 仍只应放可信局域网。直播直连 B 站所需的
  TLS 客户端是另一个议题（Q1/P4），不改变配置服务的信任模型。
- 不改时间语义：无 RTC、无 tzdata、目标仍按本地墙上时间解释，倒计时仍严格绑定时钟。

### 0.4 设计摘要（TL;DR）

- **配置**：v3 在 v2 上增加 `pages[]`（`clock`/`split`/`countdown`/`live`）、
  `defaultPageId`、全局 `live{source,url,refreshSec,timeoutMs}`；每页 live 固定 4 行
  （平台/字符串房间号/启用/屏幕主指标）；最多 8 页、5 个直播页、20 个直播间。
- **迁移**：v1→v2→v3 在 `ConfigStore::load` 一次完成；v2 补 3 页（时钟/分屏/倒计时），
  默认第 1 页；新装模板 4 页（多一个空直播页）。`POST /api/config` 只收 v3。
- **屏幕**：时钟/倒计时整屏；分屏上 10 行时钟 + 下 6 行倒计时；直播 **2×2**，
  每格 26×8。4 个直播间同屏可读；物理 4 条 4px 文本行在 5 行字体下不可行（§5.7 / Q2）。
- **编辑器**：页面列表（类型/标题/默认/当前/排序/删除/新增）+ 单页表单 + 4 行直播编辑 +
  跨页直播监控；未保存提示、聚合校验、390px 适配。
- **模拟器**：按选中页预览，草稿即时生效；直播有 自动/示例/加载/无数据/错误/未开播/空页
  预览；沿用圆点/辉光/缩放约定，文本改用与固件同源的 TomThumb 位图。
- **API**：`GET/POST /api/config` v3、`POST /api/page`（当前页，不写配置）、
  `GET /api/live/status`（快照/能力表/错误码）、`/api/status` 增加 `page`。
- **直播真实数据**取决于传输方案（B 站直连 TLS / 局域网中转 / 暂不抓取），见 Q1；
  页面系统与直播配置/占位不依赖它，可先交付。

---

## 1. 现有实现约束（设计必须遵守的事实）

### 1.1 显示与字体

- 有效显示区 **52×16**；SPI 帧补齐到 **64×16**，帧间隔不得低于 15 ms（`PageBase::sendLedData`）。
- 字体为 **TomThumb**：位图高 5 行，数字可见宽 3 px，冒号可见宽 1 px，字距另加；
  `TimerPage` 的 `measureText` 用“可见宽度 + spacing”而不是 `xAdvance`。
  - 参考：`16:38:29` 在 scale=2 时原始宽度 54 px，`drawScaledText` 会把字距从 2 收紧到 1
    后得到 47 px，仍在 52 px 内。
- **16 行最多放 3 行 5 px 高的文本**（3×5=15），**放不下 4 行等高文本**。
  这是直播 4 个房间不能做成“4 条 4 px 文本行”的硬约束（详见 §5.7）。
- 字体只有 ASCII（0x20–0x7E），**中文标题不能上屏**。TomThumb 对越界字符直接跳过。
- 现有绘制约定：整屏文本居中；远目标（≥100h）用 scale=1 小字；覆盖层固定 scale=1、
  最多两行（基线 6/14）；颜色为 `#RRGGBB`。

### 1.2 时间与配置

- 设备无 RTC、无 tzdata，系统时区固定 UTC；`local = utc + tzOffsetMinutes`。
- 时间有效阈值 `now >= 1700000000`；无效显示 `--:--:--`（`#404040`）。
- 目标存本地日期 + `HH:MM:SS`，改时区后按新时区解释；倒计时每秒 `time(NULL)` 重算，
  禁止缓存剩余秒数。
- 配置原子写 `/data/multitimer.conf`（tmp + fsync + rename）；每次网页保存写一次；
  高频状态不能写 flash（旋钮切页不得触发配置写入）。

### 1.3 网络与 HTTP

- 设备端只有 POSIX socket + 极简 HTTP **服务端**；本仓库代码内没有 HTTP 客户端、
  没有 TLS 头文件/封装。
- 但设备本身**有 TLS 条件**（`RESEARCH-live-apis.md` 实测）：`/lib/libssl.so.1.1` +
  `libcrypto.so.1.1`（OpenSSL 1.1.0i）、`/res/ui/cacert.pem`（Mozilla 根证书），
  官方 App 本身也链接 OpenSSL；限制是**没有 libcurl、没有 OpenSSL 头文件、约 35 MB 内存**。
  因此“设备直连 HTTPS”不是完全没有可能，但需要自写 HttpClient + 动态链接评估；
  B 站可行，抖音/快手仍有 cookie/签名/限流问题。
- HTTP 服务：8080，无鉴权、无 TLS；请求头 2 KB、**请求体 8 KB**（v3 提升到 16 KB）、
  接收/发送超时 3 s、最多 8 个并发连接；手机页 gzip 内嵌，网页与 API 同源。
- 平台直播接口结论（详见 `RESEARCH-live-apis.md`）：
  - **B 站**：无需 cookie/签名，`getH5InfoByRoom` 一次约 4 KB 拿全，可设备直连；
  - **抖音**：网页端点需要 `ttwid` 且会快速限流（`200` + 0 字节）；手机端点较宽松但只认
    数字 `room_id`，而 `web_rid → room_id` 的映射依赖被限流的网页端点 → **建议中转**；
  - **快手**：单房间接口需要 JS 计算的 `__NS_hxfalcon` 签名，无签名稳定被拒，
    cookie 也绕不过 → **必须中转/无头浏览器**，或明确“暂不支持”。
  - 结论：**“固件直连三平台”不可行；传输方案必须先拍板**（见 §11 Q1）。
- 网页约束：单文件、零外链（无 CDN/无 webfont/无图片）、移动优先、深色；
  现有代码是 ES5 风格（`var`/`function`），应继续沿用。

### 1.4 v2 已实现、v3 不得回归

| v2 能力 | v3 要求 |
|---|---|
| 正常时钟、`--:--:--` 无效占位 | 页面类型 `clock` 保留；开机默认页可见 |
| 带日期多目标、最多 12、顺序/去重/过期跳过 | 配置与引擎不动；分屏/倒计时页共用同一 `TimerEngine` |
| 严格绑定时钟 | 每帧取 `now` 调 `evaluate()`；改时区/对时立即反映 |
| 时区分钟偏移 -720..840 | 原样保留 |
| 时钟/倒计时/预警三色、`warnSeconds` | 原样保留；分屏/倒计时页用同一套颜色 |
| 多 WiFi 列表（GET/POST） | 原样保留 |
| 多时钟地址（localStorage） | 原样保留 |
| SoftAP 配网、中键 IP/NO WIFI 覆盖 | 原样保留 |
| 52×16 模拟器 | 升级为多页面/多分屏/2×2 直播预览 |
| 保存后热生效、状态日志、自动对时 | 原样保留 |

---

## 2. 设计原则

1. **配置声明式**：页面的类型、顺序、标题、直播行全部来自配置；设备不保存“每页隐藏状态”。
2. **帧模型共享**：固件与模拟器先用同一套规则生成 `PageFrame`（文本 run + 直播单元），
   再分别绘制；主机单测与 `check_web.js` 断言同一个模型，避免“网页预览与屏幕不一致”。
3. **时钟唯一**：分屏/倒计时页不引入第二个计时器；`Stage` 每帧只算一次。
4. **直播数据易失、配置持久**：直播快照只存 RAM，掉电丢失；配置只在用户保存时写 flash。
5. **文本即文本**：页面标题、直播标题、错误信息一律 `textContent`；LED 只渲染 ASCII 子集。
6. **失败安全**：非法页面回退 `clock`；非法直播行回退停用；保存是原子的；任何直播/网络
   失败都不影响时钟与倒计时。
7. **不新增外链**：直播数据只从设备的 `/api/live/status` 读取，网页绝不直接访问平台。

---

## 3. 配置 v3

### 3.1 完整示例

`GET /api/config` 的 `config` 字段（也是 `POST` 的请求体）：

```json
{
  "version": 3,
  "tzOffsetMinutes": 480,
  "warnSeconds": 10,
  "colors": {
    "clock": "#FFFFFF",
    "countdown": "#00FF00",
    "warning": "#FF2020"
  },
  "targets": [
    { "date": "2026-10-23", "time": "19:25:00", "enabled": true },
    { "date": "2026-10-24", "time": "08:00:00", "enabled": true }
  ],
  "live": {
    "source": "bridge",
    "url": "http://192.168.31.10:8081/live",
    "refreshSec": 60,
    "timeoutMs": 3000
  },
  "pages": [
    { "id": "p1", "type": "clock", "title": "时钟" },
    { "id": "p2", "type": "split", "title": "时钟+倒计时" },
    { "id": "p3", "type": "countdown", "title": "倒计时" },
    {
      "id": "p4",
      "type": "live",
      "title": "直播 1",
      "rows": [
        { "platform": "bilibili", "roomId": "12345", "enabled": true, "metric": "online" },
        { "platform": "douyin", "roomId": "", "enabled": false, "metric": "online" },
        { "platform": "kuaishou", "roomId": "", "enabled": false, "metric": "online" },
        { "platform": "bilibili", "roomId": "", "enabled": false, "metric": "online" }
      ]
    }
  ],
  "defaultPageId": "p1"
}
```

- 顶层字段顺序建议固定为：`version`、`tzOffsetMinutes`、`warnSeconds`、`colors`、
  `targets`、`live`、`pages`、`defaultPageId`（方便 diff 与测试）。
- 未知字段忽略（沿用 v2 约定）。
- `pages` 中非 `live` 页**不要求** `rows`；出现也忽略，不参与保存/回显。
- `live` 是全局直播数据源配置；没有直播页时也保留，默认 `source:"off"`。

### 3.2 页面模型

| 字段 | 类型 | 必填 | 规则 / 默认 | 说明 |
|---|---|---|---|---|
| `id` | string | POST 必填 | `^[a-z][a-z0-9]{0,7}$`，全页唯一 | 稳定标识；排序不变；删除后可复用 |
| `type` | string | 是 | `clock` / `split` / `countdown` / `live` | 未知类型：加载回退 `clock`；POST 400 |
| `title` | string | 否 | 0..16 个 UTF-16 单元，**≤48 字节 UTF-8**，禁止控制字符 | 空串允许；网页显示类型名兜底 |
| `rows` | array | live 可省略 | 最多 4 项；不足补空行，多了 POST 400 | 仅 `live` 有意义 |

页面类型语义：

| 类型 | 显示 | 无目标/无数据时 | 时间无效时 |
|---|---|---|---|
| `clock` | 本地 `HH:MM:SS`，scale=2，`colors.clock` | — | `--:--:--`（`#404040`，scale=2） |
| `split` | 上 10 行时钟（scale=2）；下 6 行倒计时（scale=1） | 下带 `NO TARGET`（`#404040`，scale=1） | 上下都 `--:--:--`（灰色） |
| `countdown` | 最近目标倒计时；<100h 用 scale=2，≥100h 用 `DdHH:MM:SS` scale=1 | `NO TARGET`（`#404040`，scale=1） | `--:--:--`（`#404040`，scale=2） |
| `live` | 4 个直播间单元（2×2，每格 26×8） | `NO LIVE`（`#404040`，scale=1，整屏居中） | **不受时间影响**，照常显示 |

补充：

- `split`/`countdown` 共用 v2 的 `TimerEngine::evaluate()` 与 `formatCountdown()`；
  预警色 `colors.warning` 在剩余 ≤ `warnSeconds` 时生效（仅倒计时文本）。
- `split` 的 10/6 分配是刻意选择：scale=2 需要 10 行才完整，下面 6 行刚好放 scale=1；
  若改成 8/8，上下都只能用 scale=1，时钟会明显变小。该常量集中在帧模型里，便于日后调整。
- `countdown` 页在无未来目标时**不再回退成时钟**（时钟是第 1 页的职责）；这是与 v2 单屏
  自动模式的行为差异，迁移说明见 §3.7。
- 设备屏幕**不显示页面标题**（中文无法上屏）；标题只用于网页、`/api/config`、`/api/status`。

### 3.3 直播行模型

`rows` 恰好 4 个槽位（不足补空行），顺序即屏幕顺序：

| 屏幕位置 | 数组下标 | 说明 |
|---|---|---|
| 左上 | 0 | 第 1 行 |
| 右上 | 1 | 第 2 行 |
| 左下 | 2 | 第 3 行 |
| 右下 | 3 | 第 4 行 |

每个直播行：

| 字段 | 类型 | 默认 | 规则 |
|---|---|---|---|
| `platform` | string | `bilibili` | `bilibili` / `douyin` / `kuaishou` |
| `roomId` | string | `""` | 房间号字符串，**绝不当数字**（抖音可达 19–20 位，超出 JS 安全整数） |
| `enabled` | bool | `true` | 关闭时该格不显示任何内容（全灭） |
| `metric` | string | `online` | `online` / `likes` / `fans` / `title` / `status` |

房间号规则（按平台）：

| 平台 | 规则 | 长度 | 输入示例 | 备注 |
|---|---|---|---|---|
| `bilibili` | `^[0-9]+$` | 1..12 | `12345` | 支持粘贴 `https://live.bilibili.com/12345?...` 自动提取 |
| `douyin` | `^[0-9]+$` | 1..20 | `1234567890123456789` | 支持 `https://live.douyin.com/123456`；**字符串保存** |
| `kuaishou` | `^[A-Za-z0-9_-]+$` | 1..32 | `3xabcDEF` | 支持 `https://live.kuaishou.com/u/3xabcDEF` |

- 启用行必须填合法房间号；停用行允许空/非法（但网页仍会提示，避免误以为生效）。
- 同页重复（平台+房间号完全相同）在网页上是**错误**，保存被拦截；
  跨页重复允许（共用同一份抓取数据），网页给出提示。
- 固件对重复房间**不报错**，抓取时按 `(platform, roomId)` 去重。
- 指标只校验语法；平台是否支持由**能力表**决定（见 §3.5），不支持时屏幕显示 `--`，
  网页显示“固件不支持该指标”，但**不阻塞保存**（便于固件降级/升级过渡）。

### 3.4 全局设置

| 字段 | 规则 | 说明 |
|---|---|---|
| `defaultPageId` | 必须引用存在的页面；缺省=第一页 | 仅影响**开机**；运行中切页不写它 |
| `live.source` | `off` / `bridge` / `bilibili`，缺省 `off` | `off`=不抓取；`bridge`=轮询局域网中转；`bilibili`=设备直连 B 站（P3/P4 可选） |
| `live.url` | `http://` 开头、≤160 字符、无空格；`source=bridge` 时必填 | **只接受明文 HTTP**；设备侧 TLS 客户端能力受限（见 §1.3）；其它 source 下忽略 |
| `live.refreshSec` | 整数 15..300，缺省 60 | 设备轮询周期；越大越省流量、越不容易触发平台风控 |
| `live.timeoutMs` | 整数 1000..5000，缺省 3000 | 单次 HTTP 连接+读取超时 |
| `tzOffsetMinutes` / `warnSeconds` / `colors` / `targets` | **完全沿用 v2 规则** | 不新增、不改语义 |

> 为兼容 `DESIGN-pages-v3.md` 的并行设计，最终合并时建议接受 `liveRefreshSeconds`
> 作为 `live.refreshSec` 的读取别名；**写出只写嵌套 `live`**。这样旧草案/旧页面
> 不会因字段搬家而丢刷新周期（见 §12 #9）。

### 3.5 平台能力表（以 `RESEARCH-live-apis.md` 实测为准）

| 平台 | `online` | `likes` 点赞 | `fans` 粉丝 | `title` 标题 | `status` 开播 | 数据通路 |
|---|---|---|---|---|---|---|
| `bilibili` | ✅ 人气值* | ✅ | ✅ | ✅ | ✅ | **可设备直连**（HTTPS，无 cookie/签名） |
| `douyin` | ✅ 在线观众 | ✅ | ✅（手机端点） | ✅ | ✅ | 直连需 `ttwid` 且易限流；**建议中转** |
| `kuaishou` | ✅ 仅中转 | ✅ 仅中转 | ✅ 仅中转 | ✅ 仅中转 | ✅ 仅中转 | **直连不可行**（需 JS 签名 `__NS_hxfalcon`）；必须中转/无头浏览器 |

\* **B 站 `online` 是“人气值”，不是并发人数**；UI 必须写“在线/人气”，不能写“在线人数”。
抖音的在线值是字符串（如 `"19"`/`"1.2万"`），设备需归一化为整数或显示 `--`；
抖音/快手单房间接口被拒时不能当 0，按 `errorCode` 走失败占位。

- 真值由 `GET /api/live/status` 的 `capabilities` 上报；网页在设备在线时以它为准，
  离线/旧固件时用上表兜底。
- `title` 在 LED 上只能显示 ASCII 片段；中文标题的完整内容只在网页监控里显示。
- B 站一次 `getH5InfoByRoom` 可拿全在线/点赞/粉丝/标题/状态（约 4 KB）；
  快手即使给 cookie 也绕不过签名，所以“用户填 cookie”对快手无效（调研结论）。
- 抖音 `200` + 0 字节是限流信号，必须按失败处理；不能显示成空数据（调研结论）。

### 3.6 校验、上限与体积

| 项 | 上限/规则 | 越界处理 |
|---|---|---|
| 页面数 | 1..8 | POST 400；加载截断到 8 |
| 直播页数 | ≤5（默认第 4–8 页） | POST 400（网页提前禁用）；加载只保留前 5 个直播页 |
| 每页直播行 | 4（固定槽位） | POST >4 → 400；加载截断，不足补空行 |
| 直播房间总数 | ≤20（5×4） | 由上面两条自然推出 |
| 页面标题 | ≤16 字符 / ≤48 字节 | POST 400；加载按 UTF-8 边界截断 |
| 页面 id | 1..8 字符，全局唯一 | POST 400；加载重名丢弃后者 |
| 目标数 | 12（v2 不变） | POST 维持 v2 行为（非法单项丢弃；>12 整体拒绝） |
| `live.source` | `off`/`bridge`/`bilibili` | 非法 → 400；加载回退 `off` |
| `live.url` | `http://`、≤160、无空格；**仅 `source=bridge` 时必填/校验** | bridge 缺失/非法 → 400；其它 source 忽略 |
| `live.refreshSec` | 15..300 整数，缺省 60 | 越界 → 400；加载回退 60 |
| `live.timeoutMs` | 1000..5000 整数，缺省 3000 | 越界 → 400；加载回退 3000 |
| 请求体 | **16 KB**（由 8 KB 提升） | **413** `config too large` |
| 规范化 JSON | ≤12 KB | **413** `config too large` |
| 配置文件 | 预计 ≤ 10 KB（紧凑约 6 KB） | 预算见 §3.8 |

> **请求体上限必须从 8 KB 提到 16 KB。** 8 页 × 4 行 + 12 个目标 + 48 字节标题的最坏紧凑
> JSON 约 6 KB，看似够用，但保留余量、避免未来字段增长后保存莫名失败；代价只是常量从
> `8192` 改 `16384`，设备内存完全可承受（解析峰值约 3 倍）。

### 3.7 v2/v1 → v3 迁移

**触发**：`ConfigStore::load()` 从磁盘读取时（与 v1→v2 同一位置）。

| 源 | 规则 |
|---|---|
| v1（`tzOffset`/`points`） | 先走现有 v1→v2 迁移，再执行 v2→v3 页面补全；`migrated=true`，`mainLogic` 保存一次 |
| v2（`version:2` 或存在 `tzOffsetMinutes` 且无 `pages`） | 保留 `tzOffsetMinutes`/`warnSeconds`/`colors`/`targets`；补页面；`migrated=true` |
| v3 | 直接解析；不写盘 |

v2→v3 补全规则：

- 页面固定为 **3 页**：`p1 clock "时钟"`、`p2 split "时钟+倒计时"`、`p3 countdown "倒计时"`；
  **不自动加直播页**（v2 没有直播概念，避免升级后多出一个 `NO LIVE` 空页）。
- `defaultPageId = "p1"`。
- `live = {source:"off", url:"", refreshSec:60, timeoutMs:3000}`。
- 目标/时区/颜色/预警原样保留。
- 只写一次 v3 文件（`mainLogic` 现有 `migrated` 分支）。

**行为变化（必须在文档/更新说明里写清）**：

1. v2 的“单屏自动在时钟/倒计时之间切换”被页面系统取代：第 1 页时钟、第 2 页分屏、
   第 3 页倒计时。升级后开机停在 `p1`（时钟）；有目标时倒计时在第 2/3 页。
   想要“开机看到倒计时”，把默认页设为 `p2` 即可。
2. 第 3 页在没有未来目标时显示 `NO TARGET`，不回退时钟（时钟页是第 1 页）。
3. `POST /api/config` **不再接受 v2**：返回 400
   `v2 config is not accepted, refresh the page`；v1 仍返回原有 v1 提示。
4. v3 配置对 v2 固件不可读（v2 的 `fromJson` 会因 `version != 2` 整体失败）。回滚固件时
   `/data/multitimer.conf` 不会被覆盖，但 v2 会按默认空配置运行；需要提前备份配置。

**新装默认模板**（无配置文件时 `TimerConfig()` 的默认值）：

- 4 页：`p1 clock`、`p2 split`、`p3 countdown`、`p4 live`（4 行全停用、空房间号）；
- `defaultPageId = p1`；`live = {source:"off", url:"", refreshSec:60, timeoutMs:3000}`；
  `targets` 为空。

> 新装有直播页、迁移没有，是有意为之：新装可以直接看到 v3 的完整页面结构；v2 升级
> 不额外制造空页。两种情况都可以在网页里增删页面。
>
> **并行设计差异**：`DESIGN-pages-v3.md` 建议迁移只生成 1 个 `countdown` 页
> （`countdownWhenIdle=clock`），理由是升级后屏幕与 v2 逐像素一致、老用户不会“看不到倒计时”。
> 本文件按需求里的默认页面布局给出 3 页方案。二者需要 Q5 拍板；若选 1 页，把本节
> `pages` 改成 `[{id:"p1",type:"countdown",title:"倒计时"}]`，`defaultPageId="p1"` 即可，
> 其余规则不变。

### 3.8 体积/内存预算

| 项 | 估算 | 说明 |
|---|---:|---|
| 紧凑 POST（8 页、20 行、12 目标、16 字中文标题） | ~5.5–6.5 KB | 远小于 16 KB 上限 |
| 设备落盘 pretty JSON | ~10–12 KB | jffs2 文件，原子写；/data 余量足够 |
| `GET /api/config` 响应 | ~12 KB | 无响应大小限制；浏览器解析无压力 |
| 直播快照（20 行） | ~5 KB 响应、<4 KB RAM | 标题 96 字节/行封顶 |
| 全局 `live` 配置 | <0.3 KB | source/url/refresh/timeout |
| 内嵌网页 | HTML 预计 ~140–160 KB，gzip ~35–45 KB | `editor_gz.h` 增长约 40–60 KB，`/res` 8 MiB 余量充足 |

---

## 4. 页面编辑器 UX

### 4.1 卡片顺序（保持现有风格）

1. 时钟连接（多地址，不变）
2. 网络与对时（多 WiFi、立即对时，不变）
3. 时区与显示（时区/三色/预警，不变）
4. 倒计时目标（不变）
5. **页面**（页面列表 + 选中页表单 + 直播行 + 直播数据源/刷新）
6. **直播监控**（新增）
7. **52×16 模拟器**（升级）
8. 保存
9. 状态日志（不变，始终在页面底部、不自动滚动）

资源多时允许把「页面」拆成「页面列表」「页面编辑」两张卡；v3.0 用一张卡内切换，减少滚动。

### 4.2 页面列表

```text
┌ 页面 3/8 ────────────────────────────── ┐
│ 第1页  时钟            [时钟] [默认]     │
│ 第2页  时钟+倒计时      [分屏] [当前]     │
│ 第3页  倒计时          [倒计时]          │
│                          [编辑] [↑] [↓] [✕] │
│ ...                                      │
│ [＋ 新增页面]                             │
│ 提示：最多 8 页，其中直播页最多 5 页。      │
└──────────────────────────────────────────┘
```

- 列表按 `pages` 数组顺序；序号是 **1-based** 的“第 N 页”，右侧显示总页数。
- 每行显示：序号、标题（空则类型名兜底）、类型徽标、`默认` 徽标、`当前` 徽标。
- 操作：
  - **编辑**：选中该页，下方编辑区切换到它；选中态高亮；键盘可达。
  - **↑/↓**：与相邻页交换；首/末禁用；选中页跟着 id 走，不跳页。
  - **✕ 删除**：仅剩 1 页时禁用；有直播行时确认
    「该页有 N 个直播间配置，删除后不可恢复。继续？」；删除默认页时自动把默认页
    设为相邻页（优先后一页，没有则前一页），并标记未保存。
  - **＋ 新增页面**：追加到末尾、默认 `clock`、标题“新页面”、生成未占用 id
    （`p1..p99` 里第一个空闲），选中并聚焦标题；达到 8 页禁用并提示。
- 当前页徽标来自 `device.page.id`（`GET /api/config` 或 `POST` 响应）；旋钮切页后网页
  不会自动知道，提供「刷新设备状态」小按钮（`GET /api/status`）手动同步。
- 页面顺序即旋钮顺序；顺时针下一页、逆时针上一页、循环；首/尾相接。
- 新增页的 id 在保存前不存在于设备；选中它时「设为此页」禁用并提示「先保存再切换」。

**页面列表空态**：不可能为空（至少 1 页）；若配置加载异常，网页用默认模板
（新装 4 页；识别为 v2 时 3 页）并提示。

### 4.3 页面编辑表单

选中页下方展示一个编辑区（一次只编辑一页）：

| 字段 | 控件 | 规则 | 说明 |
|---|---|---|---|
| 标题 | text | ≤16 字符、≤48 UTF-8 字节、禁控制字符 | 输入即时更新列表标题；不整块重绘以免丢焦点 |
| 类型 | select | 4 类；直播达 5 页上限时该选项禁用 | 切换类型会重建表单，保留标题 |
| 设为默认页 | switch/radio | 全配置只能一个 | 切换后 `defaultPageId` 改为本页 |
| 设为此页 | button | 仅已存在于设备的页可点 | 调用 `POST /api/page`，不保存配置 |
| 直播行 | 4 行表单 | 仅 `live` 显示 | 见 §4.4 |

类型切换细节：

- `clock`/`split`/`countdown` 之间互切没有额外确认。
- `live` → 其他类型：若任意直播行有内容，确认
  「该页的 4 个直播间配置将不再保存。继续？」；确认后 draft 里仍保留 rows（切回 `live`
  可恢复），但 `buildPayload` 只为 `live` 页输出 rows；保存后设备不再保留，重新加载即消失。
- 其他类型 → `live`：初始化 4 个空行（`bilibili`/`online`/停用）。
- 类型选项：新增页面或非直播页转直播时，达到 5 个直播页则把 `live` 置灰，并显示
  「直播页最多 5 页（共 20 个直播间）」；当前页本来就是 `live` 时，该选项保持可选，
  以便保留类型，不会把已有直播页“卡死”。

### 4.4 直播行编辑器

```text
┌ 直播间 1                          [开关] ┐
│ 平台 [B站 ▾]          指标 [在线/人气 ▾]│
│ 房间号 [ 12345                        ]  │
│ 提示：B站房间号（1–12 位数字），可粘贴直播间链接 │
│ 标题：今天的直播标题…                     │
│ 状态：正常 · 12 秒前                     │
└──────────────────────────────────────────┘
（共 4 行）
```

- 4 行固定槽位，不做增删行；不用的行停用即可。
- 平台/指标在同一行两列（390 px 下仍可并排），房间号独占一行。
- 指标文案：`online` 显示为 **在线/人气**，并在 hint 写明“B 站为人气值（非并发人数），
  抖音为在线观众数”；避免用户把人气值当人数。
- **指标联动**：
  - 平台变化时重建指标下拉；选项 = 当前能力表（设备在线时用 `capabilities`，
    离线用内置表）。
  - 原指标在新平台不支持时，自动切到该平台第一个可用指标，并显示
    「已切换为“在线/人气”（该平台不支持“粉丝”）」。
  - 已加载配置里的指标若固件不支持：保留下拉值并标注“（固件不支持）”，
    行内黄色提示，但不阻塞保存（LED 显示 `--`）。
  - 平台变化时房间号**保留**，但按新平台校验；格式不符立即标红，避免静默丢数据。
- **房间号校验**：
  - 去首尾空白；B站/抖音去掉内部空白；快手不允许空白。
  - 纯数字按平台长度校验；不是纯数字时尝试按平台链接提取：
    - `live.bilibili.com/<digits>`、`live.douyin.com/<digits>`、`live.kuaishou.com/u/<id>`；
    - 去掉 query/hash/末尾斜杠；其他域名/短链（如 `b23.tv`）拒绝并提示
      「请粘贴对应平台的直播间地址或房间号」。
  - 输入框长度限制：B站 12、抖音 20、快手 32；`inputmode` 数字/文本随平台切换。
  - 房间号**始终按字符串处理**，不 `parseInt`；19/20 位抖音号必须原样保存。
  - 启用行为空 → 错误；停用行为空 → 允许。
- **重复处理**：
  - 同页两个**启用**行 `(platform, roomId)` 相同 → 两行标红，保存拦截，
    错误文案「第 4 页第 2 行与第 3 行重复：bilibili 12345」。
  - 跨页重复 → 行内提示「该直播间已在第 3 页使用（共用抓取数据）」，不阻塞。
  - 停用行不参与重复判断。
- **行状态徽标**：`已停用` / `未填写` / `格式错误` / `重复` / `正常`；
  有直播快照时再叠加 `正常/加载/无数据/未开播/错误`（只读，来自设备）。
- **行标题**：来自 `/api/live/status` 的 `title`，`textContent` 渲染、CSS 截断，
  绝不 `innerHTML`；这是不可信文本（见 §7.6）。
- **空页/全停用**：允许保存；页面卡片提示「设备显示 NO LIVE」。

#### 4.4.1 直播数据源（全局）

直播行只描述“看谁”，真正的数据从哪来由全局 `live` 决定。放在直播行下方，
默认折叠，避免普通用户误改：

| 字段 | 控件 | 规则 | 说明 |
|---|---|---|---|
| `live.source` | select | `关闭` / `局域网中继` / `直连 B 站` | 默认关闭；直连 B 站只对 B 站行有效 |
| `live.url` | text | 仅 `http://`，≤160 字符，无空格；中继必填 | 例：`http://192.168.31.10:8081/live`；不做外链跳转 |
| `live.refreshSec` | number | 15..300，默认 60 | 设备轮询周期；越大越省流量 |
| `live.timeoutMs` | number | 1000..5000，默认 3000 | 高级项，默认收起 |

联动与提示：

- 选“中继”时显示 URL 输入；选“关闭”/“直连 B 站”时隐藏 URL，但**保留值**便于切回；
  提交时仍携带 `live.url`，firmware 在非 `bridge` 时忽略（不报错）。
- 选“直连 B 站”时提示「抖音/快手行将显示 `--`；要三平台请用中继」。
- 选“关闭”时提示「直播页显示占位，不产生任何网络请求」。
- 保存前校验：bridge + 空/非 http URL → 拦截，聚焦 URL。
- 设备返回 `device.capabilities.liveSource === false` 时，整段只读并提示固件不支持数据源配置；
  直播行仍可编辑/保存（避免升级过渡期丢配置）。
- 能力表（§3.5）决定指标下拉；直连 B 站时把抖音/快手指标标灰并提示。

**空态**：没有直播页时该段仍显示（配置可先填好）；source=off 时监控显示“数据源未启用”。

### 4.5 直播监控

新增独立卡片，展示**全部已配置直播行**（跨页），不仅当前页：

- 汇总条：`正常 n · 加载 n · 过期 n · 未开播 n · 异常 n · 未启用 n`；设备离线/未联网单独提示。
- 数据源摘要：`live.source`、`source.reachable`、`lastOkUtc`、`errorCode`；
  source=off 时显示“数据源未启用”；bridge 不可达时显示“中继不可达”。
- 按页面分组：
  - 页面标题（中文可正常显示）、页序号；
  - 每行：平台名 + 房间号（文本，不可点外链）、屏幕主指标及其值、
    **全部可用指标**（在线/点赞/粉丝/标题/开播）、状态徽标、数据年龄、错误映射文案。
  - 标题最多两行省略；数字同时显示紧凑值与完整值（如 `1.2K (1,234)`）。
- 数据来源：`GET /api/live/status`；轮询规则：
  - 仅当页面 `document.visibilityState === "visible"`、存在至少一个启用直播行、
    且已配置时钟地址时轮询；
  - 间隔 15 s；失败退避 30/60/120 s；成功后恢复 15 s；
  - 404 → 判定“固件无直播状态接口”，停止轮询并显示说明（不刷错误日志）；
  - `document.hidden` 时暂停，恢复可见立即拉一次；
  - 手动「刷新」按钮只重新 GET 快照，不触发设备抓取。
- 错误展示：行内错误文案 + 卡片顶部一条汇总；**只在首次失败和恢复时写状态日志**，
  避免每 15 s 刷日志。
- 隐私/安全：房间号与标题只在本机网页显示；不生成任何平台外链、不跳转。

### 4.6 空态、加载、错误（网页）

| 场景 | 页面表现 |
|---|---|
| 未填地址/连不上设备 | 页面/直播编辑仍可用（draft + 默认模板）；保存报错；模拟器可离线预览 |
| `GET /api/config` 非 2xx / 非法 JSON | 默认模板 + 顶部 warning + 状态日志完整错误；模拟器可用 |
| 固件返回 version 2 | 顶部横幅「设备固件为 v2，页面与直播配置只读；请升级固件」；保存禁用；模拟器可预览模板 |
| `/api/live/status` 404 | 监控卡「固件未提供直播数据接口」；行状态 `unsupported`；保存仍可用 |
| 直播行全停用/空 | 监控「还没有启用的直播间」；设备 `NO LIVE` |
| 无未来目标 | 第 3 页 `NO TARGET`；分屏下半 `NO TARGET`；编辑器提示 |
| 时间未同步 | 时钟/分屏/倒计时页 `--:--:--`；直播页正常 |

### 4.7 保存、校验与未保存修改

**保存前校验顺序**（聚合，不提前退出）：

1. 时钟地址非空（为空则直接报错，不发请求）。
2. 全局：`tzOffsetMinutes`、`warnSeconds`、三色、`live.source/url/refreshSec/timeoutMs`。
3. 目标：沿用 v2 的日期/时间/上限校验。
4. 页面：1..8 页、id 唯一、类型合法、标题长度/字符、直播页 ≤5。
5. 直播行：每页 4 行；启用行的平台/房间号/指标；同页重复。
6. 默认页必须存在。

错误展示：

- 顶部 `role="alert"` 错误条：第一条错误 + 总数；保存按钮上方，不自动滚动页面。
- 字段级：`.invalid-t` + `aria-invalid="true"` + 行内错误文案。
- 状态日志：完整 bullet 列表（含页码/行号），沿用现有 `<pre>` 文本渲染。
- 有错时**不发 POST**，聚焦第一个非法控件。

**未保存修改（dirty）**：

- 判定：`buildPayload(draft)` 序列化与“上次成功加载/保存的基线”不同即 dirty；
  选中页、预览状态、直播轮询、状态日志不算修改。
- 表现：标题旁 `未保存修改` 徽标；保存按钮保持可用。
- 拦截：
  - 点「重新加载」、切换时钟地址、切换已有地址、刷新/关闭标签页（`beforeunload`）
    时若 dirty → `confirm`「有未保存的页面/直播修改，确定放弃？」；取消则保持原状。
  - WiFi 列表保存是独立文件，不受页面 dirty 影响（沿用 v2），但保存 WiFi 不应清 dirty。
- 保存成功：重新 `GET /api/config` 作为新基线，清 dirty；保留“当前选中页”（按 id，
  不存在则回退默认页）；刷新当前/默认徽标；静默对时一次（v2 行为）。
- 保存失败：保留 dirty，错误条 + 日志；不重新加载、不对时。
- 页面内所有输入在 `input/change` 后即时更新 draft；**不要每次按键重建整块 DOM**，
  只更新对应文本/徽标，避免手机端焦点丢失。

**直播数据源/刷新**：数据源、URL、刷新周期与超时的控件见 §4.4.1；它们都是全局设置，
不属于某一页，无直播页时也保留。

### 4.8 移动端 390 px

- `main` 仍 `max-width:480px`、`padding:14px 12px`；390 px 视口内容宽约 366 px。
- 所有 flex 文本子项 `min-width:0`；长房间号/标题 `overflow-wrap:anywhere`；
  页面标题单元 `overflow:hidden;text-overflow:ellipsis;white-space:nowrap`。
- 页面行操作按钮 44×44，允许换行；不允许出现固定宽 >340 px 的元素。
- 直播行：`grid-template-columns: minmax(0,1fr) minmax(0,1fr)`；select/input 宽度 100%。
- 输入/select 字体 ≥16 px（沿用现有 iOS 防缩放规则）；按钮最小 44 px。
- 模拟器 canvas 按父容器宽度计算 cell（`min(avail/52, 9)`），容器 `overflow:hidden`。
- 不引入横向滚动容器；`pre` 继续 `word-break:break-all`。
- 验收用 390×844 与 320×568 各看一遍：`scrollWidth <= clientWidth`，按钮不被裁切。

### 4.9 可访问性

- 所有 `input/select` 有可见 label 或 `aria-label`；图标按钮 `title` + `aria-label`。
- 页面行用独立「编辑」按钮选中，不做整行 `div` 点击，保证键盘可操作。
- 直播行用 `fieldset` + `legend`（或等价 aria 分组）。
- 错误条 `role="alert"`；监控汇总 `aria-live="polite"`（低频更新）。
- 删除/切换类型等破坏性操作均有 `confirm`；焦点在新增/删除后落到合理控件。
- 不依赖颜色 alone：状态同时有文字徽标。

### 4.10 旋钮切页（固件行为，网页只读回显）

- 顺时针 = 下一页，逆时针 = 上一页；顺序 = 页面数组顺序；默认首尾循环。
- 一个刻度一页，不做加速度/长按重复；只有 1 页时忽略。
- 切页立即更新当前页（RAM），下一个 1s tick 重绘；可显示 800 ms `P<index>/<count>`
  覆盖层（如 `P2/4`）。若当前已有 IP/AP 覆盖层，仍切页但**不覆盖**该覆盖层。
- 当前页变化**不写配置/flash**（若 Q4 选持久化，按稳定 30s + 间隔 120s 节流写
  `StoragePreferences`，不写 `/data/multitimer.conf`）。
- 中键短按 IP/NO WIFI、长按 3s SoftAP 的行为**完全不变**；旋钮按键不分配功能（v3.0）。
- 网页“设为此页”走同一路径：`POST /api/page {"id":...}`，不保存配置。
- 重启后回到 `defaultPageId`（若 Q4 选不持久化；见 §11 Q4）。

---

## 5. 52×16 渲染契约与模拟器

### 5.1 帧模型（固件与模拟器共享）

渲染前先构造 `PageFrame`，两边用同一套规则；它是主机单测和 `check_web.js` 的断言对象。

| 字段 | 含义 |
|---|---|
| `pageId` / `pageType` | 当前页标识与类型 |
| `timeValid` | 设备时间是否有效（直播页无影响） |
| `overlay` | 是否处于覆盖层（IP/AP/切页提示） |
| `runs[]` | 文本 run：`text`、`color`、`scale`（1/2）、`baseline`、`boxX`、`boxWidth`、`align` |
| `cells[]` | 直播单元（仅 live）：`index`(0..3)、`rect`、`state`、`text`、`color` |
| `summary` | 给 `/api/status`/模拟器 meta 的一行 ASCII 摘要 |

规则：

- 一个 run 的文本先按“初始字距 = scale”测量；超宽则把字距收紧到 0；仍超宽时：
  - 整屏 run（boxWidth=52）按 52 截断（沿用 v2 行为）；
  - 直播单元 run（boxWidth=26，最大 24）从末尾删字符直到放得下。
- 所有 run 的文本进入绘制前统一转大写 ASCII；仅 v2 远目标的 `d` 例外（保留 TomThumb 小写 `d`）。
- `scale=2` 的字形高 10 行，`scale=1` 高 5 行；基线即字形底边。
- 直播单元的空/停用行不生成 run（该格全灭）。

### 5.2 版面几何（固定值，测试必须钉住）

| 元素 | boxX | boxWidth | baseline | scale | 最大文本宽 | 说明 |
|---|---:|---:|---:|---:|---:|---|
| 整屏大字（时钟/倒计时） | 0 | 52 | 13 | 2 | 52 | v2 现行为：上边 3，下边 12 |
| 整屏小字（≥100h/占位） | 0 | 52 | 10 | 1 | 52 | v2 现行为：上边 5，下边 9 |
| 分屏上半（时钟） | 0 | 52 | 10 | 2 | 52 | 占 0..9 行 |
| 分屏下半（倒计时） | 0 | 52 | 15 | 1 | 52 | 占 10..14 行 |
| 直播单元 1（左上） | 0 | 26 | 6 | 1 | 24 | 字形占 1..5 行 |
| 直播单元 2（右上） | 26 | 26 | 6 | 1 | 24 | 同上 |
| 直播单元 3（左下） | 0 | 26 | 14 | 1 | 24 | 字形占 9..13 行 |
| 直播单元 4（右下） | 26 | 26 | 14 | 1 | 24 | 同上 |
| 覆盖层第一行 | 0 | 52 | 6 | 1 | 52 | 沿用 v2 |
| 覆盖层第二行 | 0 | 52 | 14 | 1 | 52 | 沿用 v2 |

ASCII 版面：

```text
整屏时钟/倒计时（scale 2）        分屏（上 10 / 下 6）
0  ┌────────────────────────┐     0  ┌────────────────────────┐
   │                        │        │      16:38:29          │  scale 2
   │       16:38:29         │        │                        │
   │                        │      9  ├────────────────────────┤
   │                        │        │      02:15:09          │  scale 1
15 └────────────────────────┘     15  └────────────────────────┘

直播 2×2（每格 26×8）
0  ┌────────────┬───────────┐
   │ B1.2K      │ D3.4K     │
7  ├────────────┼───────────┤
   │ K--        │ B12K      │
15 └────────────┴───────────┘
```

- 分屏下半基线 15：字形占 10..14，第 15 行留空，视觉上略靠上但避免越界；
  若今后要更居中，只能改成 8/8 或压缩字体，不在 v3.0 范围。

### 5.3 各页面类型的 run

| 类型 | run 数量 | 内容 | 颜色 |
|---|---|---|---|
| `clock` | 1 | 本地 `HH:MM:SS` scale=2；无效 `--:--:--` | `colors.clock` / `#404040` |
| `split` | 2 | 上：时钟 scale=2；下：`HH:MM:SS`/`DdHH:MM:SS`/`NO TARGET` scale=1 | 上 `colors.clock`；下 `colors.countdown`/`colors.warning`/`#404040` |
| `countdown` | 1 | `<100h` `HH:MM:SS` scale=2；`≥100h` `DdHH:MM:SS` scale=1；无目标 `NO TARGET` scale=1；无效 `--:--:--` scale=2 | 倒计时/预警色；占位 `#404040` |
| `live` | 0..4 | 每个启用且有房间号的行一个单元 run | 按 §5.4 |
| 直播页无启用行 | 1 | `NO LIVE` scale=1 整屏居中 | `#404040` |
| 覆盖层 | 1..2 | `IP x.x.x.x` / `AP ...` / `P2/4` | 调用方指定 |

#### 5.3.1 严格绑定时钟（不变量）

- 每个渲染帧只取**一次** `now = time(NULL)`，`TimerEngine::evaluate(now)` 只算**一次**；
  分屏上下两半、时钟、倒计时共享同一个 `Stage`/`now`，禁止各半区分别取时间。
- **禁止**缓存剩余秒数并递减；禁止用 `CLOCK_MONOTONIC` 算倒计时（只可用于覆盖层/陈旧判定）。
- 对时/改时区后下一帧立即按新 `now`/`tzOffsetMinutes` 重算；画面允许最多 1 个 tick 的延迟。
- 网页模拟器同样把 `now` 作为 `buildPageFrame(now, ...)` 的显式输入，纯函数、无内部计时；
  手动跳时或改时区会立即改变上半/下半文本。
- 对应测试：M4（两半同步）、M13（跳时立即变）、R4（严格时钟）。

### 5.4 直播单元状态与占位

| 状态 | 单元格文本 | 颜色 | 触发条件 |
|---|---|---|---|
| `ok` | `TAG` + 数值（如 `B1.2K`）/ `TAG` + `LIVE` / `TAG` + 标题 ASCII 片段 | `colors.clock` | 最近一次抓取成功且在新鲜期内 |
| `stale` | 同上（保留最后一次值） | `#808080` | 抓取失败但有旧数据，或数据过旧 |
| `loading` | `TAG...` | `#404040` | 已配置、刚启动/刚保存、尚无结果 |
| `error` | `TAGERR` | `colors.warning` | 从未成功且最近一次失败（含无 WiFi） |
| `offline` | `TAGOFF`（数值指标也可 `TAG--`） | `#404040` | 平台明确返回未开播 |
| `unsupported` | `TAG--` | `#404040` | 固件/平台不支持该指标，或指标值缺失 |
| `disabled` | 无 run（全灭） | — | 行停用 |
| `empty` | 无 run（全灭） | — | 启用但房间号为空（正常保存不会出现；加载会归一化为停用） |
| 整页无启用行 | `NO LIVE`（整屏居中） | `#404040` | 所有行停用/为空 |

- 新鲜期：`ageSeconds <= max(2 * live.refreshSec, 90)` 且最近一次抓取成功 → `ok`；
  超过该年龄（即使最近成功）或最近失败但仍有旧数据 → `stale`；从未成功且最近失败 →
  `error`；从未尝试且 WiFi 可用 → `loading`；WiFi 不可用且无旧数据 → `error`（`no_wifi`）。

- 平台 TAG：`bilibili→B`、`douyin→D`、`kuaishou→K`。
- **同平台多房间的辨识**：v3.0 最小实现用 1 字符 TAG；如果同页有多个 B 站/抖音房间，
  建议合并时改用自动 `label`（平台字母 + 房间尾 2 位，如 `B25`、`D88`），
  文本形如 `B25 1.2K`。在 24 px 预算内 `label(3)+gap(1)+value(4)` 最宽约 22–24 px，
  仍可放下；具体 fit 阶梯见并行设计 §2.4 / §12 #7。v3.0 不提供手填 label。
- `title` 指标：把标题按字节过滤到 0x20–0x7E，转大写，压缩连续空格，取前 **5** 个
  字符；若一个都没有 → `TAG--`。中文标题通常只能得到 `TAG--`，这是字体限制，
  不是 bug；完整标题见网页监控。
- 数值缺失/为负/非数字 → `TAG--`；`status` 指标缺失 → `TAG--`。
- 停用行不生成 run；同一页允许部分格全灭，形成“部分房间”版式。
- `live.source=off`：所有启用行按 `unsupported`（`TAG--`，灰色）显示，不显示 `ERR`，
  避免用户没配数据源时整屏红色；网页监控显示“数据源未启用”。

### 5.5 数字格式（固件与网页必须一致）

| 输入 n（整数） | 输出 | 例 |
|---|---|---|
| 0..999 | 十进制 | `0`、`999` |
| 1000..9999 | 一位小数 K，去掉 `.0` | `1K`、`1.5K`、`10K` |
| 10000..999999 | 四舍五入到 K；若结果达到 1000K 则用 1M | `12K`、`999K`、`1M` |
| 1000000..9999999 | 一位小数 M，去掉 `.0` | `1.2M` |
| 10000000..999999999 | 四舍五入到 M | `12M`、`999M` |
| ≥1000000000 | 一位小数 B / 四舍五入 B | `1.2B`、`12B` |
| 负数/NaN/缺失 | `--` | — |

- 后缀大写（`K/M/B`），不用中文“万”。
- 最长为 4 字符（`999M`/`1.2B`），加 TAG 共 5 字符，在 24 px 内可放下。
- 网页监控同时显示完整千分位值，避免“1.2K”精度歧义。

### 5.6 模拟器

**页面选择**：

- 模拟器卡顶部新增 `预览页面` 下拉：`1. 时钟`、`2. 时钟+倒计时`……；
  与页面列表选中项双向同步（选列表 → 下拉更新；选下拉 → 列表高亮）。
- 预览的是**草稿**（未保存也生效），不是设备最后保存的配置；dirty 时显示“预览（未保存）”。
- 删除当前预览页后，选择相邻页（优先后一页）。
- 页面类型变化后，下拉文字同步（`3. 倒计时`）。

**直播状态预览**（仅当选中页是 `live` 时显示）：

| 选项 | 预览内容 |
|---|---|
| 自动（设备数据） | 使用最近一次 `/api/live/status` 快照；无快照时等同 `loading` |
| 示例数据 | 固定样本（如 `B1.2K D3.4K K-- B12K`），用于离线查看 2×2 版式 |
| 加载中 | 所有启用格 `TAG...` |
| 无数据 | 所有启用格 `TAG--` |
| 错误 | 所有启用格 `TAGERR`（warning 色） |
| 未开播 | 所有启用格 `TAGOFF` |
| 空页 | 所有行停用 → `NO LIVE` |

- 示例数据是唯一允许“伪造数值”的选项，必须明确标注“示例数据，不来自设备”。
  即使当前页 4 行全停用，“示例数据”也合成 4 个示例单元，用于预览 2×2 版式；
  其余预览状态在无启用行时显示 `NO LIVE`。
- 时间控制（跟随真实时间/手动日期时间/播放倍速）对时钟、分屏、倒计时页保持 v2 行为；
  直播页与时间无关，控制仍可操作但 meta 文案说明“直播页不依赖时间”。

**模拟器 meta 文案**（替换现有 `stageText`）：

- 时钟：`第 1/4 页 · 时钟 · 16:38:29（UTC+08:00）`
- 分屏：`第 2/4 页 · 时钟+倒计时 · 16:38:29 / 02:15:09（UTC+08:00）`
- 倒计时预警：`第 3/4 页 · 倒计时 · 00:00:07（预警红色）`
- 无目标：`第 3/4 页 · 倒计时 · NO TARGET`
- 直播：`第 4/4 页 · 直播 · B1.2K D3.4K K-- B12K · 数据 12 秒前`
- 直播空页：`第 4/4 页 · 直播 · NO LIVE（没有启用的直播间）`
- 时间无效：`第 1/4 页 · 时钟 · --:--:--（设备时间未同步）`

**绘制约定（沿用现有 LED 模拟器）**：

- 画布仍是 52×16 个圆形 LED；cell 大小 `min((容器宽-14)/52, 9)`，DPR ≤3；
- 背景 `#05070A`；未亮点 `rgb(24,27,35)`（`#181B23`）；亮点为目标色 + `shadowBlur≈0.55*cell`
  的辉光；圆点半径 `0.43*cell`；
- 只有绘制 key 变化才重绘（页面 id、各 run 文本/颜色/scale、直播快照版本、画布宽度）；
- 1 秒时间节拍沿用现有 rAF 循环；直播数据不参与动画，轮询到新快照才重绘；
- 不做闪烁/滚动/渐变；加载态是静态 `...`（与固件 1 Hz 一致）。

**字体光栅（v3 的必要升级）**：

- 现有模拟器用 canvas `fillText` + 超采样近似字形；多分屏/2×2 下无法保证基线、字距、
  截断与固件一致，且 scale=2 文本放不进 10 行带会裁切。
- v3 改为**内嵌 TomThumb 子集位图**（5 行、可见宽 3/1/2 px、advance=可见宽、字距规则
  与固件一致），直接生成 52×16 的 0/1 覆盖，再走现有圆点/辉光绘制。
- 子集只需：`A-Z`、`0-9`、空格、`.`、`:`、`/`、`-`、`~`、`d`（远目标小写 d 保留）。
- 字体数据必须与 `src/Fonts/TomThumb.h` 同源；`check_web.js` 增加逐字节比对测试，
  防止网页字体漂移。复制 TomThumb 位图需在 `THIRD_PARTY_NOTICES.md` 保持 BSD-3-Clause 说明。
- 这样“模拟器看到的”与固件 `drawScaledText` 的测量/截断/字距完全一致，
  也是分屏和 2×2 预览可信的前提。圆点、辉光、缩放、DPR、配色等既有绘制约定不变，
  只把文本光栅从 canvas 字体换成与固件同一份位图。

### 5.7 “4 行直播”可行性结论（重要）

需求写的是“一页 4 个直播间”“4 行直播布局”。在 52×16 + TomThumb 下：

- TomThumb 字形高 **5 行**；4 行等高文本需要行距 4 px，必然裁掉字形或与邻行重叠；
- 3 行文本已经占 15/16 行，没有空间做第 4 行；
- 要真做 4 行，只能新增 3×4 或 4 px 高的专用字体；数字勉强、字母几乎不可读，
  还要新增字库/固件渲染路径，不值得。

因此 v3.0 的结论：

| 方案 | 同屏 4 间 | 可读性 | 固件改动 | 结论 |
|---|---|---|---|---|
| **2×2 网格（推荐）** | 是 | 好（每格 26×8，5 行字体） | 仅渲染器 | **采用** |
| 4 条 4 px 文本行 | 是 | 不可读/裁字 | 新字体 + 渲染器 | 否决 |
| 2 行 + 每 5 秒翻页（2+2） | 分时显示 | 好 | 需要定时/翻页状态 | 备选，v3.1 可加 |
| 单行轮播 4 间 | 分时显示 | 最好 | 需要定时/翻页状态 | 不满足“一页 4 间” |

- 编辑器仍是 **4 行**（4 个直播间配置），屏幕按 **2×2** 呈现；这是“4 行配置 →
  2×2 显示”的映射，必须在 UI 提示里说清，避免用户以为坏了一行。
- 如果坚持“物理 4 行文本”，需要拍板接受新字体与可读性下降（见 §11 Q2）。

---

## 6. API 契约

### 6.1 端点总表

| 方法 | 路径 | v3 变化 |
|---|---|---|
| GET | `/`、`/index.html` | 返回 v3 内嵌页（仍是 gzip、零外链） |
| GET | `/api/config` | `config` 为 v3；`device` 增加 `page`/`capabilities` |
| POST | `/api/config` | **仅接受 v3**；请求体上限 16 KB；保存后热应用 |
| POST | `/api/page` | **新增**：运行中切换当前页，不写配置 |
| GET | `/api/live/status` | **新增**：直播快照/能力表/错误码 |
| GET | `/api/status` | `state` 增加 `page`；`mode` 增加 `live` |
| POST | `/api/time` | 不变 |
| GET | `/api/wifi`、POST `/api/wifi`、POST `/api/wifi/list` | 不变 |
| OPTIONS、`/favicon.ico` | 不变 |
| 其余 | 404 `{"ok":false,"error":"not found"}` |

CORS 头保持现状；网页与设备同源时无影响，`file://` 打开时仍可跨域访问（`*`）。

### 6.2 `GET /api/config`

```json
{
  "ok": true,
  "config": { "...§3.1..." },
  "device": {
    "ip": "192.168.31.124",
    "ssid": "JasonYiL",
    "wifi": true,
    "uptimeSec": 715,
    "timeUtc": 1792658309,
    "timeValid": true,
    "tzOffsetMinutes": 480,
    "localTime": "2026-10-22 16:38:29",
    "lastSyncUtc": 1792658309,
    "page": {
      "id": "p2",
      "index": 2,
      "count": 4,
      "type": "split",
      "title": "时钟+倒计时",
      "defaultId": "p1"
    },
    "capabilities": {
      "configVersion": 3,
      "pages": true,
      "liveStatus": true,
      "liveSource": true,
      "pageSwitch": true
    }
  }
}
```

- `page.index` **1-based**；`page.count` 为总页数；`page.defaultId` 回显默认页。
- `page`/`capabilities` 是新增字段；v2 网页会忽略它们，不破坏兼容。
- **命名区分**：`device.capabilities` 是固件**功能位**（configVersion/pages/liveStatus/liveSource/
  pageSwitch）；平台→指标的**直播能力表**在 `GET /api/live/status.capabilities`，两者不是一回事。
- 设备从磁盘读取失败/无文件时返回默认模板（4 页 + 空目标），`ok` 仍为 `true`。
- 配置里的 `defaultPageId` 若磁盘上非法，GET 返回内存归一化后的值（第一页），
  但不会为此写盘。

### 6.3 `POST /api/config`

请求：v3 配置对象（允许 `{"config":{...}}` 包裹，沿用 v2）。

严格校验（与磁盘加载的宽松归一化不同）：

| 条件 | 结果 |
|---|---|
| JSON 非法 / 根不是对象 | 400 `invalid json` / `config root must be an object` |
| `version` 缺失或 ≠3 | 400 `version must be 3`；含 v1/v2 字段给更明确的刷新提示 |
| `pages` 缺失/非数组/为空/超过 8 | 400，错误含 `pages` |
| 页面 id 非法/重复 | 400 `pages[i].id ...` / `duplicate page id: pX` |
| 类型未知 | 400 `pages[i].type ...` |
| 标题超长/含控制字符 | 400 `pages[i].title ...` |
| 直播页 >5 / 行 >4 | 400 `too many live pages` / `pages[i].rows ...` |
| 启用行房间号非法 | 400 `pages[i].rows[j].roomId: invalid for <platform>` |
| 指标/平台未知 | 400 `pages[i].rows[j].metric ...` |
| `defaultPageId` 存在但找不到 | 400 `defaultPageId: not found` |
| `live.source`/`live.refreshSec`/`live.timeoutMs` 越界；`source=bridge` 时 `live.url` 缺失/非法 | 400，错误含 `live.` 字段名 |
| 请求体 >16 KB / 规范化 JSON >12 KB | **413** `config too large` |
| 保存失败 | 500 `save config failed` |

- 非 `live` 页出现 `rows`：忽略（不报错）。
- `rows` 少于 4：**补齐的槽位一律 `enabled:false`、`platform:bilibili`、`roomId:""`、
  `metric:online`**；只有显式给出的行对象才按 `enabled` 缺省 true 处理。`rows` 整体缺失
  等同 4 个停用空行，避免“缺省启用 + 空房间”被误判为 400。
- 房间号重复：**允许**（抓取去重）。
- 指标语法合法但平台不支持：**允许**（显示 `--`），由网页用能力表提前引导。
- 目标字段维持 v2 行为：非法单项丢弃、>12 整体拒绝（不因 v3 改变 v2 契约）。

成功响应：

```json
{
  "ok": true,
  "page": { "id": "p2", "index": 2, "count": 4, "type": "split",
            "title": "时钟+倒计时", "defaultId": "p2" }
}
```

- 保存前当前页 `p2`；保存后仍存在 → 不变。
- 当前页被删除 → 夹到 `min(oldIndex, count-1)`，响应里回传实际页。
- `defaultPageId` 改变**不会**让设备立即跳页；只影响下次开机。要立即跳页用 `/api/page`。

### 6.4 `POST /api/page`

请求：

```json
{ "id": "p4" }
```

响应：

```json
{ "ok": true, "page": { "id": "p4", "index": 4, "count": 4,
                        "type": "live", "title": "直播 1", "defaultId": "p1" } }
```

规则：

- 只切运行中的当前页；**不写配置、不写 flash**、不改变默认页。
- 未知/已删除 id → 400 `page not found: pX`。
- 已在该页 → 200 幂等（仍可返回 page）。
- 与旋钮切页同一路径：更新内存当前页 + 下一个 1 s tick 重绘；可显示 800 ms
  `P<index>/<count>` 覆盖层（若当前已有 IP/AP 覆盖层则不打断它）。
- 重启后回到 `defaultPageId`（当前页不持久化；见 §11 Q4）。

### 6.5 `GET /api/live/status`

**需要这个接口。** 原因：

1. 网页监控要显示“设备看到的实时数据/年龄/错误”，不能靠网页直连平台（CORS/反爬/外链）；
2. 模拟器“自动”预览必须与服务端上屏内容同源，否则预览会骗人；
3. 能力表由固件上报，网页据此做指标联动，避免网页与固件能力漂移；
4. 设备离线/旧固件时，网页可据此优雅降级（404/空能力表）。

响应：

```json
{
  "ok": true,
  "wifi": true,
  "source": {
    "kind": "bridge",
    "url": "http://192.168.31.10:8081/live",
    "reachable": true,
    "lastOkUtc": 1792658309,
    "errorCode": ""
  },
  "refreshSec": 60,
  "fetchedAt": 1792658309,
  "nextRefreshSeconds": 42,
  "capabilities": {
    "bilibili": ["online", "likes", "fans", "title", "status"],
    "douyin": ["online", "likes", "fans", "title", "status"],
    "kuaishou": ["online", "likes", "fans", "title", "status"]
  },
  "rows": [
    {
      "pageId": "p4",
      "row": 1,
      "platform": "bilibili",
      "roomId": "12345",
      "state": "ok",
      "live": true,
      "ageSeconds": 12,
      "metrics": { "online": 1234, "likes": 5678, "fans": 90123 },
      "title": "今天的直播标题",
      "errorCode": ""
    },
    {
      "pageId": "p4",
      "row": 2,
      "platform": "douyin",
      "roomId": "",
      "state": "disabled",
      "live": null,
      "ageSeconds": -1,
      "metrics": {},
      "title": "",
      "errorCode": ""
    }
  ]
}
```

字段：

| 字段 | 说明 |
|---|---|
| `wifi` | 设备当前是否联网；false 时抓取暂停 |
| `source` | 数据源健康摘要：`kind`（off/bridge/bilibili）、`url`、`reachable`、`lastOkUtc`、`errorCode` |
| `refreshSec` | 当前生效的刷新周期（回显 `live.refreshSec`） |
| `fetchedAt` | 最近一次成功抓取的 UTC epoch；0=从未成功 |
| `nextRefreshSeconds` | 距下一轮抓取的整数秒；无启用行/暂停时 -1 |
| `capabilities` | 平台→指标数组；空对象=固件未启用直播能力 |
| `rows[]` | **按配置顺序**输出所有直播行（含停用/空行），最多 20 行 || `pageId` | 该行所属页面 id |
| `row` | 1-based（1..4），与编辑器“直播间 N”一致 |
| `state` | `ok`/`stale`/`loading`/`error`/`offline`/`disabled`/`empty`/`unsupported` |
| `live` | 开播状态；未知 null |
| `ageSeconds` | 距该行最近成功数据的秒数；-1=从未成功。用单调时钟计算，避免对时跳变 |
| `metrics` | 仅包含有值的受支持指标；整数；缺失=没取到/不支持 |
| `title` | 不可信 UTF-8 文本，≤96 字节（UTF-8 边界截断）；空串允许 |
| `errorCode` | ASCII 短码（见 §6.8），最长 24 字节 |

- `capabilities` 随数据源变化：`source=bridge` 可报三平台；`source=bilibili` 只报
  `bilibili`；`source=off` 报空对象（网页可用内置表编辑，但监控显示“数据源未启用”）。
- 网页的指标下拉以 `capabilities` 为准；直连 B 站时抖音/快手行显示“仅中继可用”。

行为：

- 快照只在 RAM，掉电丢失；重启后 `loading`。
- 抓取周期由 `live.refreshSec` 控制；抓取失败保留旧值，状态转 `stale` 并在
  web 显示错误码；从未成功 → `error`。
- 同一快照可被多次 GET；不因 GET 触发抓取（避免网页刷接口导致风控）。
- 无直播行时 `rows: []`、`nextRefreshSeconds: -1`。
- 旧固件没有该路径 → 404；网页按“不支持直播状态”处理，不影响配置保存。

**可选（P3 再加）**：`POST /api/live/refresh` 触发一次立即抓取（10 s 冷却，429 表示太快）。
v3.0 不要求；网页的“刷新”只重读快照。

#### 6.5.1 中继契约（Q1 选 A / `source=bridge` 时）

设备 → 中继：**明文 HTTP GET**，不带 cookie/token；中继负责平台签名、限流、cookie。

```text
GET {live.url}?rooms=bilibili:12345,kuaishou:3x8k2m9,douyin:88776655
Accept: application/json
```

- 最多 20 个房间；room 做 URL 编码；请求体为空；`live.timeoutMs` 作为整次超时。
- 建议每轮一次批量请求，避免 20 次小请求。

中继 → 设备（≤4 KB，UTF-8）：

```json
{
  "ok": true,
  "rooms": [
    { "platform": "bilibili", "room": "12345", "live": true,
      "online": 1234, "likes": 5678, "fans": 90123,
      "title": "今天的直播标题", "error": "" },
    { "platform": "kuaishou", "room": "3x8k2m9", "live": false,
      "online": 0, "likes": 0, "fans": 0,
      "title": "", "error": "" }
  ]
}
```

- 缺少某个 room → 该房间 `state:error`（`bridge_protocol`），其它房间正常。
- `ok:false` → 全页 `error`；`200`+0 字节按失败处理（抖音限流同类）。
- `title` 不可信，设备端只截 ASCII 片段；网页监控用 `textContent` 显示。
- 中继不可达/超时 → `bridge_unreachable`；JSON 非法/超 4 KB → `bridge_protocol`。
- 中继返回 `online` 语义由平台决定：B 站是人气值，抖音是在线观众；网页标签统一写“在线/人气”。

### 6.6 `GET /api/status` 扩展

```json
{
  "ok": true,
  "device": { "...同 GET /api/config..." },
  "state": {
    "mode": "countdown",
    "text": "16:38:29",
    "remainSeconds": 123,
    "targetUtc": 1792660000,
    "targetLocal": "2026-10-23 19:25:00",
    "warning": false,
    "smallFont": false,
    "overlay": false,
    "page": { "id": "p2", "index": 2, "count": 4,
              "type": "split", "title": "时钟+倒计时", "defaultId": "p1" }
  }
}
```

- `mode`：`clock` / `countdown` / `invalid` / `live`。分屏页按“是否存在未来目标”
  报 `countdown` 或 `clock`；`page.type` 才是权威类型。
- `text`：当前页**主 run** 的文本（分屏=上半时钟；直播=第一个非空单元或 `NO LIVE`）；
  不是像素级快照。需要精确数据用 `/api/live/status` 与页面类型。
- `remainSeconds`/`targetUtc`/`targetLocal` 只在 `mode=countdown`（分屏/倒计时页）时有意义；
  `clock`/`live`/`invalid` 时分别为 0/0/空串。直播页不会因为后台有目标就报告倒计时。
- 原有字段保持含义；v2 消费方不会因新增 `page` 报错。

### 6.7 保存热生效时序

```text
网页                         设备 HTTP 线程                     UI 线程（1s tick）
 |-- POST /api/config (v3) -->|
 |                            |-- 严格校验
 |                            |-- 原子保存 /data/multitimer.conf
 |                            |-- 内存应用（页面/直播目标/刷新周期）
 |                            |-- 当前页按 id 保留/夹取
 |<-- 200 {ok,page} ----------|
 |-- GET /api/config -------->|  （回读 canonical）
 |-- POST /api/time --------->|  （静默对时，v2 行为）
 |-- GET /api/live/status --->|  （有直播行时）
 |                            |                          |-- 下一个 tick 换配置
 |                            |                          |-- 生成 PageFrame + 送 LED
```

- **HTTP 线程绝不直接写 SPI**；`setConfig`/`setCurrentPage` 只更新互斥保护的状态，
  由 UI 线程 tick 绘制。最坏延迟 1 s，满足“保存热生效”。
- `POST /api/page` 同路径：更新当前页 → 下一个 tick 绘制（+ 可选切页覆盖层）。
- 配置保存失败不应用；内存配置保持旧值。
- 直播配置应用时：保留仍被引用的缓存，丢弃已删除房间的缓存，新房间进入 `loading`；
  刷新周期变化时重排下一次抓取。

### 6.8 错误码

HTTP 错误体沿用 `{"ok":false,"error":"..."}`；固件错误字符串保持 ASCII 短句，
网页原样展示并补中文摘要。

| HTTP | `error` 示例 | 场景 |
|---:|---|---|
| 400 | `invalid json: ...` | 请求体不是 JSON |
| 400 | `version must be 3` | 版本不对 |
| 400 | `v2 config is not accepted, refresh the page` | 旧缓存页提交 v2 |
| 400 | `v1 config is not accepted, refresh the page` | 更旧页面 |
| 400 | `pages must have 1..8 items` | 页数 |
| 400 | `duplicate page id: p2` | id 重复 |
| 400 | `pages[3].rows[1].roomId: invalid for douyin` | 房间号 |
| 400 | `live.url: required when source=bridge` | 中继地址缺失 |
| 400 | `live.url: must start with http://` | 中继地址非 http |
| 400 | `live.refreshSec out of range (15..300)` | 刷新周期 |
| 400 | `live.timeoutMs out of range (1000..5000)` | 超时 |
| 400 | `defaultPageId: not found` | 默认页不存在 |
| 413 | `config too large` | 请求体 >16 KB 或规范化 JSON >12 KB |
| 404 | `page not found: p9` | `/api/page` |
| 404 | `not found` | 未知路径 |
| 500 | `save config failed` | 落盘失败 |
| 503 | `too many connections` | 并发上限（现有） |

直播 `errorCode`（`/api/live/status`）：

| 码 | 网页文案 | 说明 |
|---|---|---|
| `""` | 正常 | 无错误 |
| `no_wifi` | 设备未联网 | 抓取暂停 |
| `timeout` | 请求超时 | 连接/读取超时 |
| `network` | 网络错误 | DNS/连接失败 |
| `http_401`/`http_403` | 平台拒绝访问 | 可能需要 Cookie/签名 |
| `http_404` | 房间不存在 | 房间号错误/已关闭 |
| `http_429` | 请求过于频繁 | 限流，退避 |
| `http_5xx` | 平台服务异常 | 5xx |
| `parse` | 数据解析失败 | 响应格式变化 |
| `bridge_unreachable` | 中继不可达 | bridge 连接/超时 |
| `bridge_protocol` | 中继响应不合法 | 非 JSON/缺字段/超 4 KB |
| `source_off` | 数据源未启用 | `live.source=off` |
| `unsupported` | 固件不支持 | 无适配器/指标不支持 |
| `blocked` | 平台风控 | 需要验证/登录 |
| `rate_limited` | 平台限流 | 退避后重试 |
| `tls` | TLS 不可用 | 直连 B 站时传输层缺失 |
| `unknown` | 未知错误 | 兜底 |

### 6.9 能力协商与降级

| 设备情况 | 网页行为 |
|---|---|
| 无 `device.capabilities`（旧固件/手工服务） | 视为 v3：页面可编辑；直播按“接口未知”处理 |
| `device.capabilities.configVersion < 3` 或 `config.version === 2` | 顶部横幅；页面/直播编辑只读；保存禁用；模拟器用默认模板 |
| `liveStatus:false` 或 `/api/live/status` 404 | 直播配置仍可保存（v3）；监控显示“固件未提供直播数据接口”；LED `TAG--` |
| `device.capabilities.liveSource:false` | 数据源段只读；保留配置原值；提示固件不支持 |
| `live.source=off` | 不发起任何请求；LED 4 格 `TAG--`；监控显示“数据源未启用” |
| 直播能力表缺某平台 | 该平台指标下拉为空/仅默认；行显示“固件不支持”；保存允许 |
| 保存返回 400 `version must be 3` | 提示升级固件；保持 dirty；不重试 |

> v3 网页通常由 v3 固件内嵌提供；上面主要是 `file://` 打开新页面对旧设备、
> 或固件升级过渡期的兜底。

---

## 7. 交互细节与安全

### 7.1 保存校验清单（必须全部覆盖）

- [ ] 时钟地址非空；地址合法（沿用现有 `normalizeBaseUrl` 规则）。
- [ ] `tzOffsetMinutes` -720..840 整数；`warnSeconds` 0..600 整数。
- [ ] 三色 `#RRGGBB`。
- [ ] 目标：日期 `2000..2099` 合法日历日、时间 `HH:MM:SS`、≤12。
- [ ] `live.source` 合法；`source=bridge` 时 `live.url` 为 http://、≤160、无空格；
      `live.refreshSec` 15..300；`live.timeoutMs` 1000..5000。
- [ ] 页面 1..8；id 唯一且符合规则；类型合法；标题 ≤16 字符/≤48 字节/无控制字符。
- [ ] 直播页 ≤5；每页恰好 4 行（内部固定）。
- [ ] 启用行：平台合法、房间号符合平台规则、指标合法。
- [ ] 同页启用行 `(platform, roomId)` 不重复。
- [ ] `defaultPageId` 指向存在的页面。
- [ ] 构造 payload 后再次自检：`version===3`、无 `_` 前缀 UI 字段、无备注、无 WiFi 密码。
- [ ] 保存中禁用保存按钮，防重复提交；失败恢复。

### 7.2 未保存修改提示

- dirty 定义与 UI 见 §4.7。
- `beforeunload`：dirty 时设置 `event.returnValue`（浏览器显示原生提示）；
  非 dirty 不弹提示（handler 可以常驻，内部判断 dirty 即可）。
- 切换时钟地址、点重新加载、离开页面均需确认。
- 保存成功/失败的状态必须一致：成功清 dirty，失败保留。
- 直播轮询、预览页选择、状态日志不改变 dirty。
- 页面内“设为此页”不保存配置，不应清 dirty，也不应因 dirty 被禁用（只要 id 已存在）；
  它跳转到设备**已保存**的该页内容，草稿里的改名/换类型要保存后才在屏幕上体现。

### 7.3 页面顺序与当前页

- 顺序 = 旋钮顺序 = 列表顺序；保存后立即生效。
- 默认页只影响开机；当前页只影响本次运行。
- 删除当前页：设备保存后夹到相邻页；网页用响应/回读更新“当前”徽标。
- 删除默认页：网页自动改默认页并在保存时提交；不允许提交不存在的默认页。
- 旋钮在覆盖层（IP/AP）显示期间仍可切页，但不覆盖覆盖层；覆盖结束后显示新页。
- 只有一页时旋钮无效果（不显示切页覆盖层）。

### 7.4 390 px 无横向溢出

- 规则见 §4.8；实现时禁止新增固定宽布局、禁止 `100vw`、禁止对长文本用 nowrap。
- 静态自检（`check_web.js`）扫描 CSS 中明显超宽常量；最终以浏览器 390×844 手动验证为准
  （DOM 桩无法计算真实布局，这一点必须在测试说明里写清）。
- 重点检查：页面行 4 个操作按钮、直播行平台/指标并排、长房间号、中文标题、
  模拟器 canvas、状态日志 `pre`。

### 7.5 0 JS error / 无外链

- 所有 `JSON.parse` 包裹 try/catch；`localStorage` 读写包裹 try/catch（沿用 v2）。
- 访问 `document.visibilityState` 前判空；不支持时视为 visible。
- 所有 DOM id 必须存在（`check_web.js` 静态断言），避免首屏 null 异常。
- 所有按钮 `type="button"`，避免表单提交刷新。
- 零外链：无 `<script src>`、`<link>`、`@import`、`url(http)`、媒体标签、ES module、
  Worker、`fetch("http...")`、`<a href="http...">`；页面内只允许请求 `{base}/api/*`。
- 平台域名只作为“粘贴链接解析”的字符串，不用于 fetch/链接。

### 7.6 XSS 与不可信文本

不可信来源：页面标题、直播标题、房间号（服务端回显）、设备错误串、SSID、WiFi 列表、
`/api/live/status` 的 `title`/`errorCode`。

规则：

- 只用 `textContent`、`value`、`setAttribute` 写数据；`innerHTML` 只允许赋 `""` 清空。
- 不拼接 HTML 字符串；不生成 `<img>/<script>/<svg>` 等元素。
- 直播标题显示前截断（≤120 字符）并压缩空白；不把标题写进 `title` 属性时更安全，
  若写也必须 `setAttribute`。
- 错误详情放 `<pre>`，`textContent`。
- 数字值先 `Number.isFinite`/范围检查，非法显示 `--`。
- 设备端：页面标题只存不画；直播标题只提取 0x20–0x7E 大写片段；房间号有白名单正则。
- `check_web.js` 必须覆盖：标题/直播标题/错误串/回显房间号带 HTML 时不产生元素、
  不执行脚本。

### 7.7 隐私与安全边界

- HTTP 服务仍无鉴权/无 TLS，只应在可信局域网使用；`POST /api/page` 同权限。
- 页面标题、房间号、数据源 URL、刷新周期明文存 `/data/multitimer.conf`；WiFi 密码仍单独存
  `/data/multitimer.wifi`，不进网页 localStorage、不进日志（v2 规则不变）。
- 若用中继，平台 cookie/签名只应存在于中继，**不要下发到设备或网页**；设备只拿归一化后的
  房间数据（在线/点赞/粉丝/标题/开播）。
- 网页不向任何第三方发请求；直播抓取只发生在设备（或用户决定的中转），
  网页只读设备快照。
- 房间标题可能含敏感内容；只在本地网页显示，不写入配置、不外传。
- 平台抓取需遵守平台条款；抓取频率默认 60 s，可在 15..300 s 内调整（建议 30 s 以上）。

---

## 8. 固件侧结构调整建议（不写代码，只定职责）

> 本节是为了让 API/渲染/直播的职责边界清晰；具体头文件签名由实现阶段冻结。

| 文件 | 职责 |
|---|---|
| `src/timer/TimerTypes.h` | 扩展：`PageType`、`LivePlatform`、`LiveMetric`、`LiveRowConfig`、`PageConfig`、`LiveConfig`；`TimerConfig` 增加 `pages`、`defaultPageId`、`live` |
| `src/config/ConfigStore.*` | v3 JSON 解析/序列化/严格 POST 校验/宽松加载归一化/v1→v2→v3 迁移 |
| `src/pages/PageText.*`（新） | 从 `TimerPage` 抽出的纯文本测量/绘制（TomThumb、字距收紧、截断）；不依赖平台头 |
| `src/pages/PageRenderer.*`（新） | `buildPageFrame(config, page, stage, now, timeValid, liveSnapshot)` + `drawPageFrame(Surface&, PageFrame)`；纯 C++11，可主机单测 |
| `src/pages/TimerPage.*` | 唯一 FlyThings 页面：当前页索引、旋钮事件、覆盖层、1 s tick、`PageStatus` 快照；不再自己拼字符串 |
| `src/live/LiveTypes.h`（新） | `LiveRowData`、`LiveSnapshot`、`LiveCapabilities`、`LiveFetcher` 接口 |
| `src/live/LiveService.*`（新） | 抓取调度/缓存/退避/去重/状态 JSON；注入 `LiveFetcher`，主机测试用假 fetcher |
| `src/live/Platform*.{h,cpp}`（P3） | B 站/抖音/快手适配器；HTTPS/中转实现按 §11 Q1 决策 |
| `src/net/HttpServer.*` | v3 API、`/api/page`、`/api/live/status`、16 KB 体限、能力/页面信息 |
| `src/logic/mainLogic.cc` | 启动顺序、ApplyCallback、LiveService 启动、中键行为不变 |
| `web-src/index.html` | v3 页面（本文件 §4/§5） |
| `tools/check_web.js` | 新增测试点（§9） |
| `tests/test_page_config.cpp`（新） | schema/迁移/校验/上限/房间号/指标 |
| `tests/test_page_render.cpp`（新） | 帧几何/文本/颜色/状态/格式/截断 |
| `tests/test_live_status.cpp`（新） | 缓存/状态机/退避/去重/JSON/能力表（假 fetcher） |

要点：

- `PageManager` 仍只注册一个 `TimerPage`；“页面”是应用内渲染模式，不是 FlyThings 的
  Activity/Page，避免页面注册/生命周期复杂度。
- 旋钮事件在 `TimerPage::onKeyEvent` 处理（`E_KEYCODE_CLOCKWISE`/`ANTI_CLOCKWISE`，
  `KeyManager` 对旋钮只发 status 0）；中键长按仍由 `mainLogic` 处理。
- 线程：配置/直播快照用互斥锁；HTTP 线程只写状态，UI tick 才 `sendLedData`。
- 64 位时间/计数避免 `std::atomic<long long>`（Z21 无 libatomic），沿用现有互斥锁方案。
- 直播调度用 `CLOCK_MONOTONIC` 计算间隔/退避，避免网页对时跳变导致“永远不到点”；
  `fetchedAt` 用 wall clock 仅用于展示。

---

## 9. `check_web.js` 新增测试点清单

> 不写代码。每项给“测什么 + 断言要点”。编号为新增组；现有 61 项中依赖 v2 固定
> 假设的断言（`version:2`、`REQUIRED_IDS=40`、`buildPayload` 只含 v2 字段等）
> 必须在 P1 同步更新，而不是简单追加。

### 9.1 静态/结构（S）

| ID | 测试点 | 断言要点 |
|---|---|---|
| S1 | 脚本可编译、可执行 | `new Function` 编译通过；全部沙箱 0 运行期异常 |
| S2 | 零外链 | 无 script/link/@import/url(http)/媒体/模块/Worker；无 `fetch("http`；无 `<a href=http` |
| S3 | 必需 DOM id 完整 | 新增 `pageList/pageEditor/pageTitle/pageType/defaultPageChk/setCurrentPageBtn/addPageBtn/liveEditor/liveRows/liveSource/liveUrl/liveRefresh/liveTimeout/liveMonitor/liveMonitorList/simPageSelect/simLiveState/simMeta/saveError` 等；断言总数同步更新 |
| S4 | 无危险 HTML | `innerHTML` 只允许 `""`；无内联 `on*`；无 `document.write/eval/insertAdjacentHTML/outerHTML` |
| S5 | 390 px 静态体检 | CSS 无固定宽 >340 px 的块、无 `100vw`、长文本类无 `white-space:nowrap`；`min-width:0` 存在 |
| S6 | 字体同源 | 内嵌字形表与 `src/Fonts/TomThumb.h` 对应字符逐字节一致；覆盖模拟器用到的全部字符 |
| S7 | 无平台外链 | `bilibili.com/douyin.com/kuaishou.com` 只出现在链接解析字符串中，不进入 fetch/链接 |

### 9.2 配置 v3（C）

| ID | 测试点 | 断言要点 |
|---|---|---|
| C1 | 默认模板 | 4 页 p1 clock/p2 split/p3 countdown/p4 live；default=p1；live 4 空行；refresh=60 |
| C2 | normalize 保留结构 | 顺序/id/type/title/rows/refresh 原样；未知字段丢弃；坏类型回退 |
| C3 | v2 迁移（网页侧） | 无 pages 的 v2 fixture → 3 页（clock/split/countdown）；tz/warn/colors/targets 不变；default=p1 |
| C4 | 页面 id/default 归一化 | 缺 id 生成；重名去重；default 缺失/指向不存在 → 第一页 |
| C5 | 页面上限 | 第 9 页新增被禁；仅剩 1 页时删除被禁；5 个直播页后新增/转 live 被禁（当前 live 页不受影响） |
| C6 | 直播行归一化 | 4 槽位补齐/截断；platform/metric 缺省；19 位 roomId 保持字符串 |
| C7 | 全局 `live` | source 枚举；bridge 必填 http URL；refreshSec 15..300；timeoutMs 1000..5000；越界/非法拦截 |
| C8 | `buildPayload` 精确性 | `version=3`；页面顺序；live 页恰 4 行；无 `_note/_touched/selectedPageId/previewState`；包含 default/refresh |
| C9 | 最坏 payload 体积 | 8 页/20 行/12 目标/16 字标题时紧凑 JSON <12 KB（并记录实际值） |
| C10 | 旧固件模式 | `config.version=2` → 横幅、页面只读、保存禁用、无 v3 POST |
| C11 | v2 页面被拒 | 模拟设备返回 400 `v2 config...` → 错误展示、dirty 保留、无回读 |

### 9.3 页面编辑器（P）

| ID | 测试点 | 断言要点 |
|---|---|---|
| P1 | 页面列表渲染 | 行数、序号 1-based、类型/标题/默认/当前徽标正确 |
| P2 | 选中与绑定 | 点“编辑”选中；表单回填标题/类型/默认；列表高亮 |
| P3 | 改名 | 输入即时更新列表标题与 draft；不整块重建（输入元素引用不变） |
| P4 | 类型切换 | clock/split/countdown 互切；live 显示直播行；live→clock 有确认；切回可恢复 rows |
| P5 | 默认页 | 单选；改默认只改 `defaultPageId`；删除默认页自动改默认并 dirty |
| P6 | 新增页 | 追加/选中/聚焦标题/id 唯一；8 页禁用；直播上限时**新增/非直播页转 live** 不可选（当前 live 页仍保持） |
| P7 | 排序 | 上移/下移交换；首末禁用；选中页按 id 跟随；默认/当前不随排序错位 |
| P8 | 删除页 | 确认文案；最后一页禁止；删除后选中相邻页；默认/当前处理 |
| P9 | 设为此页 | 已保存页 → POST `/api/page {id}`；成功更新当前徽标；新页/无地址禁用；失败显示错误 |
| P10 | 标题校验 | >16 字/超 48 字节/控制字符 → 行内错误、保存拦截 |
| P11 | 标题 XSS | `<img onerror>` 标题只作文本，不产生元素 |
| P12 | 列表空态/异常 | 页面数组异常时回退默认模板并提示，不抛异常 |
| P13 | 未保存修改拦截 | 编辑后 dirty 徽标出现；`beforeunload` 设置 `returnValue`；点重新加载/切地址弹确认，取消不丢 draft；保存成功清 dirty，保存失败保留 |
| P14 | 当前页与草稿区分 | 改标题/类型未保存时点“设为此页”仍跳设备**已保存**的该页；新页禁用并提示 |

### 9.4 直播行编辑器（L）

| ID | 测试点 | 断言要点 |
|---|---|---|
| L1 | 4 行固定结构 | 每行：开关/平台/指标/房间号/提示/状态；共 4 行 |
| L2 | 指标联动 | 平台变化重建指标选项；不支持值自动回退并提示；选项来自能力表 |
| L3 | 房间号规则 | B 1..12 数字、D 1..20 数字、K 1..32 字母数字_-；启用空值报错、停用空值允许 |
| L4 | 链接解析 | 三平台 URL 提取；query/hash 清理；错误域名/短链拒绝 |
| L5 | 大数房间号 | 19/20 位抖音号以字符串保存/回读，无精度丢失 |
| L6 | 同页重复 | 平台+房间号相同 → 两行标红、保存拦截、错误含页码/行号；不同平台同数字允许 |
| L7 | 跨页重复 | 允许并提示“共用数据”；不阻塞保存 |
| L8 | 停用/空页 | 全停用可保存；设备/模拟器 `NO LIVE` |
| L9 | 能力不支持 | 已存指标不在能力表 → 显示警告但不阻塞；LED 状态 `unsupported` |
| L10 | 无 live 接口 | `/api/live/status` 404 → 编辑仍可用、监控说明、保存仍 v3 |
| L11 | 数据源 UI | source 切换显示/隐藏 URL；bridge 空 URL 拦截；refresh 15..300；payload 含 `live.*` |

### 9.5 模拟器（M）

| ID | 测试点 | 断言要点 |
|---|---|---|
| M1 | 页面选择 | 下拉列出全部页；与列表选中同步；删除后选相邻页 |
| M2 | 时钟页 | 1 个 run：`HH:MM:SS`、scale2、baseline13、`colors.clock`；无效 `--:--:--` |
| M3 | 倒计时页 | `<100h` HH:MM:SS scale2；`>=100h` `DdHH:MM:SS` scale1；预警色；无目标 `NO TARGET` |
| M4 | 分屏页 | 2 个 run：上半 baseline10/scale2/clock 色；下半 baseline15/scale1/countdown 色；无目标 `NO TARGET`；手动跳时两半同步 |
| M5 | 直播几何 | 4 格 rect 26×8、baseline 6/14、文本最大宽 24、行序 TL/TR/BL/BR 映射 |
| M6 | 直播状态 | 示例/加载/无数据/错误/未开播/停用 → 文本+颜色与 §5.4 表一致；全停用 `NO LIVE` |
| M7 | 数字格式 | 0/999/1000/1500/9999/10000/999999/1e6/1.2e9；999999→`1M`；负数/NaN/缺失 → `--` |
| M8 | 标题指标 | ASCII 提取/大写/取 5 字；全中文 → `--`；HTML 标题不渲染元素 |
| M9 | 字体度量 | 与固件 `measureText` 一致：`16:38:29` scale2 收字距后宽 47；`B1.2K` 宽 ≤24 |
| M10 | 重绘 key | 相同帧不重复绘制；页面/文本/颜色/直播值/宽度变化会重绘；resize 重算 cell/DPR |
| M11 | 草稿预览 | 未保存修改立即反映；选择页/预览态不修改配置、不置 dirty |
| M12 | 直播快照联动 | auto 用最新快照；sample/占位覆盖稳定；监控轮询不打断 |
| M13 | 时间控制 | 跟随/手动/播放/倍速在全部页面仍工作；倒计时无递减缓存（跳时立即变） |

### 9.6 API/错误/兼容（A）

| ID | 测试点 | 断言要点 |
|---|---|---|
| A1 | GET config v3 | 解析 pages/device.page/capabilities；默认/当前徽标正确 |
| A2 | GET 失败 | 网络错误/500/非法 JSON → 默认模板、日志完整、模拟器仍可用 |
| A3 | 保存链路 | POST config → GET config → POST time 顺序；成功后清 dirty、更新徽标 |
| A4 | 保存 400 | 展示设备 error 全文 + 中文摘要；不 GET、不对时、dirty 保留 |
| A5 | 保存 500/网络错误 | 错误展示；按钮恢复；dirty 保留 |
| A6 | `/api/live/status` 200 | rows/capabilities/source/fetchedAt 解析；监控值/状态/标题/年龄 |
| A7 | live 404/500 | 监控说明；模拟器占位；不抛异常；停止轮询 |
| A8 | 轮询策略 | 仅 visible + 有启用行 + 有地址；15 s；hidden 暂停；恢复立即拉；失败退避 |
| A9 | POST `/api/page` | 成功切当前；400 未知 id；网络错误提示；不触发配置保存 |
| A10 | 能力缺省 | 无 `device.capabilities` 视为 v3；无直播能力表用内置表；configVersion<3 只读；liveSource=false 只读数据源段 |
| A11 | 体积 | 正常保存 payload <16 KB；超长场景被前端拦截或服务端 413 可读 |
| A12 | 数据源健康 | source=off 不发请求；bridge 不可达显示中继错误；直连 B 站时抖音/快手标不支持 |

### 9.7 不可信文本/安全（X）

| ID | 测试点 | 断言要点 |
|---|---|---|
| X1 | 页面标题 XSS | `<img>/<svg onload>/<script>` 只作文本；无新元素 |
| X2 | 直播标题 XSS | `/api/live/status.title` 同上；超长截断不溢出 |
| X3 | 错误串 | 设备 error / 状态日志 `<pre>` 文本渲染，不产生元素 |
| X4 | 指标异常 | 字符串/null/对象/负数/超大值 → `--` 或钳制，不抛异常 |
| X5 | 回显房间号 | 服务端返回带 HTML 的 roomId → 文本渲染；配置校验拒绝非法字符 |

### 9.8 v2 回归（R）

| ID | 测试点 | 断言要点 |
|---|---|---|
| R1 | 时区 | 下拉完整（-720..840 常用项）、自定义分钟、标签、保存/回填 |
| R2 | 颜色/预警 | 三色、`warnSeconds`、保存 payload |
| R3 | 目标 | 增删/上下移/过期移除/12 上限/状态徽标/备注 localStorage 键 |
| R4 | 严格时钟 | 手动跳 ±1h、改时区 → 倒计时与分屏下半立即变化 |
| R5 | WiFi | 列表读取/多网络/密码占位/8 上限/保存校验/重连回读 |
| R6 | 多地址 | 默认同源、添加/去重/切换/删除/重新加载 |
| R7 | 自动对时/无效时间 | >2 s 自动对时一次；无效 `--:--:--`；日志不自动滚动 |
| R8 | 全局 | 所有沙箱 0 异常；无 `scrollIntoView` 自动滚动；零外链 |

### 9.9 测试基建需要补的桩

- `document.visibilityState` + `visibilitychange` 事件分发；`document.hidden`。
- `window.addEventListener("beforeunload")` 记录 handler，可手动触发并断言
  `returnValue`/`preventDefault`。
- `fetch` fixture 支持 `/api/live/status`、`/api/page`。
- 暴露测试钩子（纯函数/只读）：`buildPageFrame(now, page, liveSnapshot)`、
  `getDraft()`、`isDirty()`、`livePollOnce()`、`setLiveSnapshot(snapshot)`、
  `getPageRefs()`；不改变正常页面行为。
- 现有 `makeFixture` 默认 `version:2` 需改为 v3（或新增 v3 fixture），
  并保留一个显式 v2 fixture 用于迁移/只读模式测试。

---

## 10. 分阶段实现建议

### 10.1 阶段划分

| 阶段 | 内容 | 依赖 | 退出标准 |
|---|---|---|---|
| **P0 设计冻结** | 本文件评审；拍板 §11 的 6 个问题；冻结 v3 schema/API/几何常量 | 用户 | 问题有结论；文档更新为“冻结” |
| **P1 配置与页面骨架** | ConfigStore v3 + v1/v2→v3 迁移；`PageRenderer`（clock/split/countdown）；旋钮切页；`POST /api/page`；`/api/config` `/api/status` 扩展；16 KB 体限；网页页面列表/编辑器/3 类页面/模拟器 3 类/保存/dirty；直播行配置先做成“可编辑但设备显示占位” | P0 | 主机单测全绿；`check_web` 全绿；真机 3 类页面 + 旋钮 + 热生效；v2 回归清单通过 |
| **P2 直播配置与状态接口（无真实抓取）** | `LiveService` 骨架 + 缓存/状态机；`/api/live/status` 返回 `loading/disabled/empty/unsupported` 与能力表；网页直播监控/状态预览/联动；模拟器 2×2 预览 | P1 | 无外网也能展示全部占位状态；监控 404/空能力降级正确；测试全绿 |
| **P3 中继/平台适配（Bridge 先行）** | 按 Q1 实现 `live.source=bridge` 的 HTTP 客户端与中转协议；B 站适配（在线/点赞/粉丝/标题/开播）；60 s 调度/退避/去重/限流；真实数字上屏与监控；B 站直连作为可选分支 | P2 + Q1 | 4 个直播间稳定刷新；错误可解释；24h 内存/稳定性初测；限流不致命 |
| **P4 抖音/快手 + 加固** | 抖音/快手适配（走中转；或明确“直连不支持”）；直连 B 站可选；能力表对齐；长稳、文档、发布 | P3；平台可行性 | 能力矩阵与 UI 一致；真机长稳；文档/验收完成 |

### 10.2 依赖关系

```text
P0 ──> P1 ──> P2 ──> P3(bridge / B站) ──> P4(抖音/快手 + 长稳)
        │        │
        │        └─ 网页监控/模拟器状态预览只依赖 /api/live/status 契约，
        │           可用假数据先做（check_web fixture），不必等真实抓取
        └─ 页面编辑/模拟器 3 类页面与直播配置不依赖任何外部平台
P3 ──> 依赖 Q1：bridge 优先 / B 站直连可选 / 暂不抓取
      └─ bridge 协议与 `live.url` 一旦冻结，网页数据源段即可定稿
```

### 10.3 建议的提交拆分（实现阶段）

1. `ConfigStore` v3 + 迁移 + 主机单测（纯 C++，无 UI）。
2. `PageText`/`PageRenderer` + 帧模型单测（先 clock/split/countdown）。
3. `TimerPage` 当前页/旋钮/状态快照 + `HttpServer` v3 config/page/status + 16 KB。
4. 网页页面列表/编辑/模拟器 3 类（此时可连真机验收 P1）。
5. 直播行编辑 + 模拟器 2×2 占位 + 监控空态（可并入 4）。
6. `LiveService` 骨架 + `/api/live/status` + 监控/状态预览（P2）。
7. bridge 客户端 + B 站中转适配 + 真实数据（P3）。
8. 抖音/快手/可选 B 站直连/长稳/文档（P4）。

### 10.4 每阶段统一验收

- `tests/run_tests.ps1` 全绿（含新测试文件；`run_tests.ps1` 的 `$appSources`
  需加入 `PageText.cpp`、`PageRenderer.cpp`、`LiveService.cpp` 等纯逻辑源）。
- `python native/MultiTimer/tools/build_web_asset.py` 重新生成 `editor_gz.h`，
  再 `node native/MultiTimer/tools/check_web.js` 全绿。
- FlyThings headless 构建 0 errors / 0 warnings（至少不新增告警）。
- 真机：`/tmp` 部署 → `GET /api/config` v3 → 旋钮切页 → 保存热生效 → 重启默认页 →
  `/api/status` 页面字段 → 断网/无效时间占位。
- 手机 390×844：无横向溢出、0 console error、0 exception；截图存 `tests/visual/`。
- v2 回归清单（§9.8）逐项通过。

### 10.5 关键风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| 平台 HTTPS/反爬 | 直播数据可能拿不到 | Q1 决定 bridge/直连；先交付 P1/P2；B 站可直连、抖音/快手走中继 |
| 4 行直播不可行 | 需求理解偏差 | §5.7 + Q2；2×2 为推荐；UI 明示 |
| 中文标题不能上屏 | 用户以为坏了 | 网页监控显示完整标题；LED 只显示 ASCII 片段/`--`；文案说明 |
| 请求体上限不足 | 保存失败 | 提到 16 KB；前端记录 payload 体积测试 |
| 抓取限流/风控 | 数据中断 | 默认 60 s、可调 15..300；退避；每轮错峰；只抓启用房间 |
| 20 房间×超时超周期 | 刷新延迟 | 顺序抓取+3 s 超时+轮转起点+不重入；失败退避 |
| 旋钮切页写 flash | 磨损 | 当前页只存 RAM；只有保存配置写 flash（若 Q4 选 B 则按 30s/120s 节流） |
| 直连 TLS 需要正确时间 | 无 RTC 首次开机证书失败 | 先 SNTP/网页对时；或选中转 HTTP |
| v3 配置回滚 v2 固件 | 屏幕回默认 | 文档提示先备份；不要回滚未验证版本 |
| 网页变大 | gzip/`/res` 预算 | 目标 HTML ≤160 KB；发布前量 `editor_gz.h` 与镜像 |

---

## 11. 需要用户拍板的问题

> 以下按影响排序。若未答复，文档以“推荐项”为默认继续细化；P3 前必须答复 Q1。

### Q1（阻塞 P3）直播数据从哪里来？

背景（实测见 `RESEARCH-live-apis.md`）：

- 设备**有** OpenSSL 1.1.0i + Mozilla CA，官方 App 自己也用；但**没有 libcurl、没有 OpenSSL
  头文件、约 35 MB 内存**，要直连得自写 TLS HTTP 客户端并评估动态链接。
- **B 站可直连**：`getH5InfoByRoom` 无 cookie/签名，一次约 4 KB 拿全；实测 20 次/10 秒无风控。
- **抖音**：网页端点需要 `ttwid`，很快 `200`+0 字节限流；手机端点宽松但只认数字 `room_id`，
  而 `web_rid → room_id` 的映射依赖被限流的网页端点。
- **快手**：单房间接口需要 JS 计算的 `__NS_hxfalcon`，cookie 也绕不过；直连不可行。

选项：

| 选项 | 说明 | 代价 | 推荐 |
|---|---|---|---|
| **A. 局域网中转（bridge）** | 用户在 PC/NAS/树莓派跑一个小服务抓三平台，输出普通 HTTP JSON；设备只轮询它 | 多一个服务要部署；`live.url` 要配；设备侧最简单、三平台都能覆盖 | **推荐**（配置已支持 `source=bridge`） |
| **B. 设备直连 B 站（+ 中转兜底）** | 设备自写 HTTPS 客户端直连 B 站；抖音/快手仍走中转或显示 `--` | 要引入 TLS 客户端/头文件/CA/内存评估；只解决 1/3 平台 | 可选 P3/P4；不建议作为唯一方案 |
| **C. v3.0 先不抓真实数据** | 页面系统 + 直播配置 + 监控占位/示例；真实抓取另立项 | 直播页只有 `TAG--`/`TAGOFF` 占位 | 可先按 C 交付 P1/P2，再上 A |

请回答：**A / B / C**；若选 A，中转程序是否由你提供/允许仓库内附带示例？
（`live.source` 已预留 `off`/`bridge`/`bilibili`，选哪个都不用再改 schema。）

### Q2 直播 4 个房间的屏幕版式？

- **A. 2×2 网格（推荐）**：4 间同屏、可读、每格 26×8；
- B. 物理 4 行：需要新增 3×4/4 px 字体，字母几乎不可读，且要改字体/渲染；
- C. 2+2 每 5 秒翻页：2 间大字号 + 自动翻页，需要固件定时器与翻页状态，预览要加“第几屏”。

请回答：**A / B / C**。当前文档按 A 写；编辑器仍是 4 行配置。

### Q3 LED 上的指标：单选还是轮播？

- **A. 每房间 1 个屏幕主指标（推荐）**：2×2 每格只显示一个值；网页监控显示全部指标；
- B. 每房间多选指标并在格内轮播：需要每格独立定时/状态，预览复杂，且会“错过”数值；
- C. 每房间 2 个指标拼一格：26 px 宽放不下。

请回答：**A / B / C**。当前文档按 A 写。

### Q4 当前页是否需要跨重启保持？

- **A. 不保持（推荐）**：当前页只存 RAM；重启回到默认页；`POST /api/page` 只切本次；
- B. 保持：每次旋钮切页写一个小状态（`StoragePreferences`）或配置，重启回到上次页；
  会引入 flash 写入/默认页语义冲突。

请回答：**A / B**。当前文档按 A 写。

### Q5 迁移后的页面结构？

- **A. 3 页（clock/split/countdown），默认第 1 页（本文件当前写法）**：升级后即可看到需求里的
  默认布局；但有目标的 v2 用户开机先看到时钟，需要把默认页改成第 2/3 页才能看到倒计时；
- B. 有未来目标时默认第 2 页分屏：更接近 v2“开机看到倒计时”，但迁移规则更“魔法”；
- C. 迁移也补一个空直播页（第 4 页）：页面结构与新装一致，但升级后多一个 `NO LIVE`；
- D. **迁移只生成 1 个 `countdown` 页（v2 逐像素等价，`DESIGN-pages-v3.md` 建议）**：
  升级后屏幕形态与 v2 完全一致（无目标回时钟），用户再自己加页；新装模板仍可以是 4 页。

请回答：**A / B / C / D**。当前文档按 A 写；若选 D，§3.7 的 `pages` 改为
`[{id:"p1", type:"countdown", title:"倒计时"}]`，其余不变（新装模板保持 4 页）。

**同一问题下的第二个选择**：`countdown` 页没有未来目标时显示什么？
本文件当前显示 `NO TARGET`；`DESIGN-pages-v3.md` 建议回时钟（v2 行为，`countdownWhenIdle`）。
请选择：**回时钟（v2 兼容）/ `NO TARGET`（本文件）/ 可配置**。
若选“回时钟”，`§3.2`、`§5.3`、M3 的 `NO TARGET` 改为时钟文本；若选“可配置”，
增加 `display.countdownWhenIdle` 全局字段（`clock`/`blank`）。

### Q6 分屏几何：8+8 还是 10+6？

- **A. 10+6（本文件，推荐）**：上半 10 行时钟用 scale 2（和整屏时钟一样大），
  下半 6 行倒计时 scale 1；时钟更醒目，下半略贴底；
- B. 8+8（`DESIGN-pages-v3.md`）：上下等分、两半都 scale 1，留白对称（2/5/2/5/2），
  但时钟只有 27 px 宽，约为整屏时钟的 55%。

两者都满足“上下分屏”；**选谁都要同步改帧模型常量、像素测试和模拟器**。
当前文档按 A 写。请回答：**A / B**。

---

## 12. 与并行设计文档的对齐（合并清单）

本轮并行产出了三份文档：

| 文件 | 视角 |
|---|---|
| **本文件** `DESIGN-web-v3.md` | 网页配置器/模拟器/编辑 UX/保存校验/API 契约 |
| `DESIGN-pages-v3.md` | 固件/显示架构（页表、渲染几何、旋钮、持久化、直播线程） |
| `RESEARCH-live-apis.md` | B 站/抖音/快手接口与设备 TLS 条件实测 |

三份文档在以下方向一致：**2×2 直播布局、4 条 4px 文本行不可行、请求体 16 KB、
`/api/page`、`/api/live/status`、LED 只上 ASCII、模拟器必须用 TomThumb 位图、
当前页不进配置文件、v3 schema 需要先冻结**。以下是仍需拍板/合并的差异
（“建议”列是本文立场；`DESIGN-pages-v3.md` §11 也有同一张表，方向基本对应）：

| # | 项 | 本文件 | `DESIGN-pages-v3.md` | 建议 |
|---|---|---|---|---|
| 1 | 分屏类型名 | `split` | `clock_countdown` | 取 `split`（短、与“分屏”一致；对方也建议 `split`） |
| 2 | 分屏几何 | 10+6，上 scale 2（基线 10/15） | 8+8，两半 scale 1（基线 7/14） | **需用户拍板**（Q6）；本文件按可读性推荐 10+6 |
| 3 | 倒计时页无目标 | `NO TARGET` | 回时钟（`countdownWhenIdle:"clock"`，v2 行为） | 建议默认回时钟以保 v2 不回归，另给“空白/NO TARGET”选项；需拍板 |
| 4 | v2 迁移页数 | 3 页（clock/split/countdown），默认第 1 页 | **1 个 `countdown` 页**（v2 逐像素等价） | 老用户建议 1 页；新装模板 4 页；需拍板（Q5） |
| 5 | 页数/房间上限 | 8 页 / 5 直播页 / 20 房间 | 12 页 / 24 房间 | 取 **8 / 20**（更保守、省 RAM；对方也建议 8/20） |
| 6 | 直播行字段 | `rows[].roomId` + `metric` | `rooms[].room` / `label` / `metric` / `color` | 合并为 `rooms[]` + `roomId` + 可选 `label`；页级 `metric` 默认 + 行级覆盖；`color` 可延后 |
| 7 | 直播单元文本 | `TAG`（B/D/K）+ 值 | `label`（B25）+ 值 | 建议采用自动 `label`（平台字母 + 房间尾 2 位）+ 值；否则 4 个 B 站房间无法区分 |
| 8 | 数值后缀 | `K/M/B` | `w/M`（万/亿） | 取 **K/M/B**（ASCII 惯例、与网页监控一致；对方也建议 K/M/B） |
| 9 | 全局直播配置 | 嵌套 `live{source,url,refreshSec,timeoutMs}`（本文件已按此更新） | 同左 | **已对齐**；`liveRefreshSeconds` 仅作读取别名 |
| 10 | 数据源 | `off/bridge/bilibili` | `off/bridge/push/bilibili` | 合并保留 `off/bridge/bilibili`；`push` 作为扩展位，v3.0 可不做 |
| 11 | 当前页持久化 | 只存 RAM，重启回默认页（Q4） | `StoragePreferences["uiState"]`，稳定 30s + 120s 节流 | 需拍板（Q4）；若选持久化，采用对方节流算法 |
| 12 | `/api/config` 增量 | `device.page` + `device.capabilities` | 顶层 `ui` + `live` 健康摘要 | 建议统一为 `device.page`（语义上属于设备）；顶层 `live` 可保留为兼容别名 |
| 13 | `/api/live/status` | `rows[]` + `capabilities` + `source` 健康 | `rooms[]` + `source` + `refreshSec` | 合并为 `rooms[]`（房间是主体）；`capabilities` 必须保留（网页联动用） |
| 14 | `/api/page` | `{"id":"p4"}` | `{"index":2}` / `{"reset":true}` | 建议支持 **id**（稳定）+ 可选 `index`；`reset` 作为 Q4=持久化时的附加能力 |
| 15 | `/api/live/refresh` | 可选（P3 再加） | 必需（旋钮按键/网页刷新） | 建议保留：网页“立即刷新”和旋钮按键都需要；P3 实现 |
| 16 | `/api/status` 增量 | `page` + `text` | `page` + `text2` + `cells` + `banner` | 合并取 `page` + `text`/`text2`；`cells` 对网页预览有用，建议保留 |
| 17 | 页面字段 | 无 `enabled`/`shortTitle` | `enabled` + `shortTitle`（切页横幅） | `enabled` 可加（默认 true）；`shortTitle` 若做横幅则需要，否则用 `P2/4`；需拍板 |
| 18 | 切页反馈 | 800ms `P2/4` | 1.2s `n/N + shortTitle` | 需拍板；本文件先按无中文横幅的最小方案 |
| 19 | 渲染节拍 | 1s tick | 250ms tick + 脏标记 | 网页模拟器 1s 即可；固件可按对方方案（不影响契约） |
| 20 | 大请求错误码 | 413 `config too large`（已改） | 413 | **已对齐** |
| 21 | `colors.dim` / `display` | 固定 `#808080`；固定行为 | `colors.dim` / `display{wrap,countdownWhenIdle,splitBottomWhenIdle}` | 可选：`dim` 固定即可；`display` 的 `wrap` 建议保留（旋钮循环可关） |
| 22 | 数据源 schema | `live.url` 必填于 bridge | 同左 | **已对齐**（仅接受 http://） |

**合并顺序建议**（避免两边返工）：

1. 冻结 **schema**：页面类型名、`rooms[]`/`roomId`/`label`、上限 8/20、`live` 对象、
   数值后缀 K/M/B、页面 id 规则。
2. 冻结 **几何**：分屏 10+6 或 8+8（Q6）、2×2 单元格 26×8、基线/字号；
   同步冻结测试期望值。
3. 冻结 **行为**：迁移页数（Q5）、倒计时无目标（回时钟/NO TARGET）、当前页持久化（Q4）、
   切页横幅。
4. 冻结 **API**：`/api/config` 增量、`/api/live/status` 字段、`/api/page` 入参、
   `/api/live/refresh` 是否必需、`/api/status` 增量。
5. 再改 `ConfigStore` / `PageRenderer` / `web-src/index.html` / `check_web.js`；
   **schema 未冻结前不要写 `ConfigStore` 和网页表单**。

> `RESEARCH-live-apis.md` 的平台接口是 2026-10-22 的实测结果，会变；
> P3/P4 实现前需要重新验证，并准备“接口失效 → `errorCode` 占位”的降级。
> 本文的 §11 与对方的 §10 合并后，才是最终需要用户拍板的问题清单。

---

## 13. 附录

### 13.1 v2 功能 → v3 落点（回归对照）

| v2 功能 | v3 落点 | 回归测试 |
|---|---|---|
| 时钟模式（无目标） | `clock` 页；分屏上半 | M2/R4 |
| 倒计时模式（最近目标） | `countdown` 页；分屏下半 | M3/M4/R4 |
| 远目标 `DdHH:MM:SS` 小字 | 同上，scale=1 | M3/M4 |
| 预警红色常亮 | 分屏下半/倒计时页 | M3/M4 |
| 无效时间 `--:--:--` | 三类页面各自占位 | M2/R7 |
| 时区分钟偏移 | 全局字段不变 | R1 |
| 三色/预警秒数 | 全局字段不变 | R2 |
| 12 目标/排序/去重/过期 | 引擎/配置不变 | R3 |
| 备注仅 localStorage | 不变（不进 v3 payload） | R3/C8 |
| 多 WiFi | 不变 | R5 |
| 多时钟地址 | 不变 | R6 |
| 自动对时/静默对时 | 保存链路不变 | A3/R7 |
| 中键 IP/NO WIFI 6 秒 | 覆盖层不变；切页覆盖层让位 | R8 |
| SoftAP 配网 | 不变 | 手工 |
| 状态日志不自动滚动 | 不变；直播错误去抖 | R8/A8 |

### 13.2 错误文案（网页侧建议）

| 场景 | 文案 |
|---|---|
| 页面标题超长 | 第 N 页标题最多 16 个字符（48 字节） |
| 页面 id 重复 | 页面标识重复：pX（请重新添加） |
| 直播页超限 | 直播页最多 5 页（共 20 个直播间） |
| 房间号为空 | 第 N 页第 M 行：启用直播间必须填写房间号 |
| 房间号格式 | 第 N 页第 M 行：<平台> 房间号格式不正确（示例：…） |
| 房间号重复 | 第 N 页第 M 行与第 K 行重复：<平台> <房间号> |
| 默认页缺失 | 默认页不存在，请重新选择 |
| 数据源 | 中继地址必须是 http:// 且不超过 160 字符；直连 B 站不支持抖音/快手；关闭后直播页只显示占位 |
| 刷新/超时 | 刷新周期 15–300 秒；超时 1000–5000 毫秒 |
| 无地址 | 未填写时钟地址，无法保存；可先在模拟器预览 |
| 固件过旧 | 设备固件为 v2，页面与直播配置不可保存；请升级固件 |
| 保存 4xx | 保存被设备拒绝：<error>（详见下方状态日志） |
| 保存失败 | 保存失败：<error> |
| 直播接口缺失 | 设备固件未提供直播数据接口；页面配置仍可保存，屏幕显示占位 |

### 13.3 页面/直播状态机（简图）

```text
直播行：
  disabled ──启用+填房号──> loading ──抓取成功──> ok ──超时/失败──> stale
      ▲                        │                 │                  │
      └──────停用/清空─────────┘                 └──平台未开播──> offline
                               └──无适配器/指标不支持──> unsupported
                               └──从未成功且失败──────> error

页面：
  任意页 ──保存──> 当前页按 id 保留（删除则夹到相邻）
  任意页 ──设为默认──> 仅影响下次开机
  旋钮/网页 ──> 当前页（RAM）──> 下一 tick 重绘
```

### 13.4 已阅读/核对的文件（只读）

- `docs/DESIGN-v2.md`、`docs/STATE-2026-10-22-v2.md`、`docs/HANDOFF-native.md`
- `docs/DESIGN-pages-v3.md`、`docs/RESEARCH-live-apis.md`（并行设计/实测，只读）
- `native/MultiTimer/web-src/index.html`（2723 行）
- `native/MultiTimer/tools/check_web.js`（2196 行，现有 61 项）
- `src/timer/{TimerTypes.h,TimerEngine.h}`、`src/pages/{TimerPage.h,TimerPage.cpp,PageBase.*}`
- `src/config/{ConfigStore.h,ConfigStore.cpp}`、`src/net/{HttpServer.cpp,mini_json.*}`
- `src/managers/{PageManager.cpp,KeyManager.cpp}`、`src/logic/mainLogic.cc`
- `src/Fonts/TomThumb.h`、`src/utils/{Surface.h,Painter.h}`
- `tests/run_tests.ps1`、`native/MultiTimer/README.md`、`native/MultiTimer/Manifest.xml`

### 13.5 文档维护

- 实现阶段如改动任何几何常量、API 字段或上限，必须同步更新本文件与
  `docs/DESIGN-v2.md` 的交叉引用（v2 仍描述 v2，不改其内容）。
- 本文件不替代 `docs/DESIGN-v2.md`；v2 的时钟/倒计时契约继续有效，v3 在其上扩展。
