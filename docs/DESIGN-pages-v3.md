# TC002 MultiTimer v3 页面/显示架构设计（可翻页页面系统 + 直播数据页）

> 状态：**设计稿（待评审，尚未实现）**。
> 上游依据：[`DESIGN-v2.md`](DESIGN-v2.md)（v2 冻结契约）、[`HANDOFF-native.md`](HANDOFF-native.md)
> （实现/线程/限制）、[`STATE-2026-10-22-v2.md`](STATE-2026-10-22-v2.md)（真机基线）、
> [`RESEARCH-live-apis.md`](RESEARCH-live-apis.md)（直播接口实测，2026-10-22）、
> 当前源码 `native/MultiTimer/src/**`、`native/MultiTimer/web-src/index.html`。
> 并行产出：[`DESIGN-web-v3.md`](DESIGN-web-v3.md)（网页配置器/模拟器设计）——两份文档的差异与合并建议见 §11。
> 本文只描述设计，不含实现代码；**未修改任何源码/脚本/设备**（仅新增本文件）。

## 0. 结论摘要

v3 在 v2 之上只做三件事：**页面列表可配置**、**旋钮翻页并恢复当前页**、**新增直播数据页**。
时钟/倒计时语义、`targets`、`tzOffsetMinutes`、`colors`、`warnSeconds` 全部保持 v2 语义不变。

| # | 决策点 | 结论 | 理由 |
|---|---|---|---|
| D1 | 配置模型 | `pages[]`（1..12）+ 全局 `targets/tz/colors/warn` + 新增 `display`/`live` | 页面顺序=数组顺序，可增删/排序；全局字段不动，v2 语义零漂移 |
| D2 | v2→v3 迁移 | 只生成 1 个 `countdown` 页（`countdownWhenIdle=clock`） | 该组合与 v2 渲染逐像素等价，老用户升级后屏幕不变 |
| D3 | 多页载体 | 单个 `PageHost`（原 `TimerPage` 重构）持有页表；`PageManager` 不改 | 热应用=vector 交换 + 下标 clamp，避免并发 register/unregister 与“注册顺序≠配置顺序” |
| D4 | 渲染层 | 抽出纯 C++ `PageRenderer`（只依赖 `Surface` + TomThumb），主机可像素级单测 | 4 种页面类型 + 分屏几何必须可回归，不能只靠真机肉眼 |
| D5 | 分屏几何 | 上半区基线 7、下半区基线 14（2+5+2+5+2 垂直节奏），两半均 scale 1 | 8px 半区放 5px 字形，上下留白各 2px，中间 2px，左右不争空间 |
| D6 | 直播页布局 | **2×2 网格**：4 个 26×8 单元格，每个房间一行 5px 文本；**4 行 ×4px 方案否决** | 4×5px=20>16，4 行必须自定义 3×4 字体且 0 行距、数字歧义；2×2 仍是一页 4 个房间且字形是原版 TomThumb |
| D7 | 单元格内容 | `<label><gap><value>`：label 1..3 字符（平台字母+房间尾号），value ≤4 字符（`1234`/`1.2w`/`9.9y`/`--`） | 实测 25px 可用宽度下，label+value 的像素预算与 fit 阶梯见 §2.4.5 |
| D8 | 直播数据源 | v3 默认走局域网 **bridge**（轮询）或 **push**（外部推送），设备端只实现明文 HTTP 客户端；**B 站直连列为可选 P4** | 设备**有** OpenSSL 1.1.0i + `cacert.pem`（见 [`RESEARCH-live-apis.md`](RESEARCH-live-apis.md)），但 SDK/工具链无头文件与 libcurl、整机 RAM 仅 ~35 MB；B 站可无 cookie 直连，抖音需 `ttwid` 且限流、快手需 JS 签名 → 只有 B 站值得直连 |
| D9 | 刷新架构 | 1 个后台线程 `LiveService` + 按页批量 1 次 HTTP + 24 条房间缓存 + 失败/陈旧占位 | 与 UI 线程完全解耦；每 refreshSec(默认30s) 最多 1 个请求 |
| D10 | 当前页持久化 | 存 `StoragePreferences["uiState"]`（`id,index,epoch`），**不进配置文件**；稳定 ≥30s 且距上次写 ≥120s 才落盘 | 旋转不写 flash；掉电最多丢 2 分钟内的页位置；≤30 次/小时 |
| D11 | 旋钮/按键 | 顺时针=下一页、逆时针=上一页（默认循环）；中键短按 IP / 长按 SoftAP **完全不变**；旋钮按键=立即刷新直播 | 保留全部既有行为；旋钮按键是最自然的“刷新”入口 |
| D12 | 渲染节拍 | `onUI_Timer` 250ms + 脏标记渲染（秒变化 / 切页 / 横幅 / 覆盖层变化才重绘） | 切页反馈 ≤250ms；稳态仍 1 帧/秒，SPI 占用不增 |
| D13 | API | `POST /api/config` 只收 v3；新增 `GET /api/live/status`、`POST /api/live/refresh`、`POST /api/page`；`/api/status` 增量字段 | 旧 v2 页面直接 400 提示刷新；新增接口满足网页预览/调试/联动 |
| D14 | 严格绑定时钟 | **每帧只取一次 `time(NULL)`**，时钟与倒计时共用同一个 `now`；禁止缓存递减 | 分屏页面如果两个半区各取一次时间，可能相差 1 秒 |

需求映射：需求 1（翻页+网页编辑）→ §1/§3/§6；需求 2（上时钟下倒计时）→ §2.2；需求 3（4 直播间/多页/指标）→ §1/§2.4/§5；
需求 4（页数/顺序/旋钮/恢复）→ §1/§3/§4。

---

## 1. 页面数据模型（JSON v3）

### 1.1 完整示例（覆盖需求里的 4 类页面）

```json
{
  "version": 3,
  "tzOffsetMinutes": 480,
  "warnSeconds": 10,
  "colors": {
    "clock": "#FFFFFF",
    "countdown": "#00FF00",
    "warning": "#FF2020",
    "dim": "#808080"
  },
  "display": {
    "wrap": true,
    "countdownWhenIdle": "clock",
    "splitBottomWhenIdle": "date"
  },
  "targets": [
    { "date": "2026-10-23", "time": "19:25:00", "enabled": true },
    { "date": "2026-10-24", "time": "08:00:00", "enabled": true }
  ],
  "live": {
    "source": "bridge",
    "url": "http://192.168.31.10:8081/live",
    "refreshSec": 30,
    "timeoutMs": 3000
  },
  "pages": [
    { "id": "p0", "type": "clock", "title": "时钟", "shortTitle": "CLOCK", "enabled": true },
    { "id": "p1", "type": "clock_countdown", "title": "时钟+倒计时", "shortTitle": "CLOCK+CD", "enabled": true },
    { "id": "p2", "type": "countdown", "title": "倒计时", "shortTitle": "COUNTDOWN", "enabled": true },
    {
      "id": "p3",
      "type": "live",
      "title": "直播 1",
      "shortTitle": "ONLINE",
      "metric": "online",
      "enabled": true,
      "rooms": [
        { "platform": "bilibili", "room": "21452505", "label": "B25" },
        { "platform": "bilibili", "room": "7734200",  "label": "B07" },
        { "platform": "kuaishou", "room": "3x8k2m9",  "label": "K3X", "metric": "likes" },
        { "platform": "douyin",   "room": "88776655", "label": "D88" }
      ]
    }
  ],
  "defaultPageId": "p0"
}
```

### 1.2 结构定义

| 层级 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 根 | `version` | int | 可缺省；存在时**必须为 3** |
| 根 | `tzOffsetMinutes` | int | 必填，-720..840（v2 语义不变） |
| 根 | `warnSeconds` | int | 0..600，缺省 10（v2 语义不变） |
| 根 | `colors` | object | `clock`/`countdown`/`warning` 同 v2；新增 `dim`（次要文字/陈旧值），缺省 `#808080` |
| 根 | `targets` | array | ≤12，项结构、校验、去重/排序、过期跳过全部同 v2 |
| 根 | `display` | object | 见 §1.2.1 |
| 根 | `live` | object | 见 §1.2.2 |
| 根 | `pages` | array | 1..12 个页面，**数组顺序 = 翻页顺序** |
| 根 | `defaultPageId` | string | 开机页（用户偏好）；缺省=第一页；运行时“最后停留页”优先于它（§4.3） |

#### 1.2.1 `display`

| 字段 | 取值 | 缺省 | 作用 |
|---|---|---|---|
| `wrap` | bool | `true` | 翻页到两端是否循环 |
| `countdownWhenIdle` | `"clock"` \| `"blank"` | `"clock"` | `countdown` 页没有未来目标时显示时钟（v2 行为）还是黑屏 |
| `splitBottomWhenIdle` | `"date"` \| `"blank"` | `"date"` | `clock_countdown` 页没有未来目标时，下半部分显示本地日期还是留空 |

#### 1.2.2 `live`（全局，不按页）

| 字段 | 取值 | 缺省 | 作用 |
|---|---|---|---|
| `source` | `"off"` \| `"bridge"` \| `"push"` \| `"bilibili"` | `"off"` | 数据来源：关闭 / 设备轮询 bridge / 外部 HTTP 推送 / 设备直连 B 站（P4 可选，见 §5.2） |
| `url` | string | `""` | `source=bridge` 时必填；**只接受 `http://`**（设备无 TLS），≤160 字符，不允许空格 |
| `refreshSec` | int | 30 | 15..300，设备轮询间隔 |
| `timeoutMs` | int | 3000 | 1000..5000，单次 HTTP 超时 |

#### 1.2.3 `pages[]`

| 字段 | 适用类型 | 取值 | 缺省 |
|---|---|---|---|
| `id` | 全部 | 1..8 字符 `[a-z0-9_-]`，页面稳定标识（用于“重启恢复当前页”，见 §4.3） | 空（按 index 恢复） |
| `type` | 全部 | `"clock"` \| `"clock_countdown"` \| `"countdown"` \| `"live"` | 必填 |
| `title` | 全部 | ≤16 字符（UTF-8，可为中文），**只给网页/`/api/config` 用，不上屏** | `""` |
| `shortTitle` | 全部 | ≤8 个可打印 ASCII（0x20..0x7E），**切页横幅用**；空则用 `title` 的 ASCII 过滤结果，再空则用类型名 | `""` |
| `enabled` | 全部 | bool | `true` |
| `metric` | `live` | `"none"`\|`"online"`\|`"likes"`\|`"fans"`\|`"gifts"`\|`"status"`\|`"score"` | `"online"` |
| `rooms` | `live` | 1..4 个房间 | 必填（1..4） |

#### 1.2.4 `pages[].rooms[]`

| 字段 | 取值 | 缺省 |
|---|---|---|
| `platform` | `"bilibili"`\|`"kuaishou"`\|`"douyin"`\|`"huya"`\|`"douyu"`\|`"custom"` | 必填 |
| `room` | 1..20 字符 `[A-Za-z0-9_-]`（B 站纯数字；快手/抖音可能含字母） | 必填 |
| `label` | 0..3 字符 `[A-Z0-9._-]`（渲染前统一大写）；空则自动生成“平台字母+房间尾 2 位” | `""`（自动） |
| `metric` | 同上枚举 | `""`（用页面 `metric`） |
| `color` | `"#RRGGBB"` | 空（用平台默认色，见 §2.4.6） |
| `enabled` | bool | `true`（false 表示该格留黑，不出数据也不轮询） |

### 1.3 校验与容错策略

**结构性错误整份拒绝（400）**，**条目级错误丢弃该条目**，**装饰性字段修正后接受**。
这与 v2（非法 target 丢弃、超限整份拒绝）一致，但把“页面是结构、房间是条目、标题/标签是装饰”分清：

| 字段 | 规则 | 违规处理 |
|---|---|---|
| `version` | 缺省或 3 | 其它值 → **400**（含 v2：`"v2 config is not accepted, refresh the page"`） |
| `tzOffsetMinutes` | 必填 -720..840 | **400** |
| `warnSeconds` | 0..600 | **400** |
| `colors.*` | `#RRGGBB` | **400**（与 v2 一致） |
| `targets` | ≤12、单项合法 | 单项丢弃；超 12 → **400** |
| `display.*` | 枚举/bool | 取缺省，不报错 |
| `live.url` | `http://` 开头、≤160、无空格 | `source=bridge` 且非法 → **400**；`source=off` 时忽略 |
| `live.refreshSec` | 15..300 | 越界 → **400** |
| `pages` | 数组、1..12 项、每项 `type` 合法 | 非数组/为空/超 12/未知 type → **400** |
| `pages[].title` | ≤16 字符 UTF-8，禁止控制字符 | 截断到 16 字符；不上屏，故不影响显示 |
| `pages[].shortTitle` | ≤8 可打印 ASCII | 超长截断；非法字符剔除；空则回退 `title` 的 ASCII 过滤结果 → 类型名 |
| `pages[].id` | `[a-z0-9_-]{1,8}` | 非法 → 视为无 id |
| `defaultPageId` | 必须引用存在的页面 id | 非法 → 回退第一页 |
| 全部页面 `enabled=false` | — | **400**（`"at least one page must be enabled"`） |
| `live` 页 `rooms` | 1..4 项 | 0 项或超 4 → **400** |
| `rooms[].platform/room` | 枚举 / `[A-Za-z0-9_-]{1,20}` | 非法单项 → 丢弃该房间（该格留黑） |
| `rooms[].label` | `[A-Z0-9._-]{1,3}`（大写后） | 非法 → 置空走自动生成 |
| `rooms[].metric` | 枚举 | 非法 → 用页面 `metric` |
| `metric=status` | 值 0/1 → `OFF`/`LIVE` | 缺值 → `--` |
| 全局直播房间总数 | ≤24 | **400**（`"too many live rooms (max 24)"`） |
| 同页重复房间 | (platform, room) 唯一 | 重复项丢弃后者 |
| 未知字段 | — | 忽略 |

### 1.4 缺省与兜底

- 配置文件不存在 / 损坏 / 版本更高：**保持内存默认**（同 v2 行为），默认 = `tzOffsetMinutes=480`、空 targets、1 个 `countdown` 页（`display` 缺省）。该组合与“v2 空配置”屏幕表现一致（时钟）。
- `pages` 全被 `enabled=false`（手工改文件绕过校验）：主机运行时回退到内置 `clock` 页，避免黑屏。
- 当前页 index 越界：clamp 到第一个 `enabled` 页（§4.3）。
- `live.source=off`：直播页照常渲染，4 格全部显示暗灰 `--`，不启动任何网络请求。

### 1.5 v2 → v3 迁移（只在 `ConfigStore::load()` 读盘时执行）

判定：
- `version == 2`，或 `version` 缺省且存在 `tzOffsetMinutes` / `targets` 且**没有** `pages` → 视为 v2；
- 存在 `tzOffset` / `points`（且无 `tzOffsetMinutes`）→ 先按 v2 现有规则迁移到 v2，再串到 v3；
- `version >= 4` → 读取失败（保留原文件），RAM 用默认配置。

映射：
- `version` → 3；
- `tzOffsetMinutes` / `warnSeconds` / `targets` 原样保留（顺序、`enabled`、日期时间文本都不改）；
- `colors.clock` / `countdown` / `warning` 原样保留；`colors.dim` 补 `#808080`；
- `display` = `{wrap:true, countdownWhenIdle:"clock", splitBottomWhenIdle:"date"}`；
- `live` = `{source:"off", url:"", refreshSec:30, timeoutMs:3000}`；
- `pages` = `[{id:"p0", type:"countdown", title:"COUNTDOWN", shortTitle:"COUNTDOWN", enabled:true}]`；
- `defaultPageId` = `"p0"`。

**为什么只生成 1 页而不是 3 页**：v2 用户升级后屏幕形态不变是最重要的兼容承诺。
若迁移成 `clock` + `clock_countdown` + `countdown` 三页，开机页会停在时钟，**原来能看到倒计时的用户升级后看不到倒计时了**，
必须去网页改默认页才能恢复——这不是“平滑迁移”。因此迁移只生成一个 `countdown` 页（§2.3 的“无目标回时钟”规则正是 v2 行为），
想要多页由用户在网页自己加。新装（无配置文件）同理使用单页默认，避免出现空直播页。

**等价性论证**：v2 屏幕只有两种形态——无未来目标时 `HH:MM:SS`（scale 2，`colors.clock`）、有目标时倒计时
（`HH:MM:SS` scale 2 / `≥100h` 用 `DdHH:MM:SS` scale 1，`colors.countdown`，`remain ≤ warnSeconds` 用 `colors.warning`）。
`countdown` 页 + `countdownWhenIdle="clock"` 就是这套规则本身（§2.3），因此迁移后逐像素一致。

其它：
- 迁移成功后由 `mainLogic` 立即 `ConfigStore::save()` 写回 v3，**只写一次**；
- v1 的“时间无效则暂缓迁移、保留原文件”规则不变（`points`→当天目标仍需要当前日期）；
- `POST /api/config` **不接受** v2：检测到 `version < 3` 或缺少 `pages` 时返回 400，提示刷新页面。

### 1.6 大小预算与请求体上限

最坏情况（12 页、其中 6 个直播页共 24 个房间、12 个目标、带缩进）估算；
若合并时采纳 §11 #7 的“8 页 / 20 房间”，下列数值约再降 30%，体限仍保持 16 KB：

| 部分 | 估算 |
|---|---|
| 12 个页面骨架（id/type/title/enabled） | ≈ 0.9 KB |
| 24 个房间（每项 ~95 B + 缩进） | ≈ 2.9 KB |
| 12 个目标 | ≈ 1.1 KB |
| 颜色/时区/display/live | ≈ 0.4 KB |
| **合计** | **≈ 5.3 KB** |

- 请求体上限：`HttpServer` 的 `kBodyMaxBytes` 由 8192 提升到 **16384**（16 KB），留 3 倍余量；
- 规范化配置序列化上限：**12288 B**，超过返回 413（防御异常 input，如超长 url）；
- 响应体：`/api/config` 最坏 ≈ 8 KB（config + device + ui + live 摘要），当前无响应上限，保持；
- 约束常量：`kMaxPages=12`、`kMaxRoomsPerLivePage=4`、`kMaxLiveRooms=24`、`kMaxTargets=12`。

### 1.7 与 v2 的兼容边界

| 项 | 行为 |
|---|---|
| 磁盘 v2 文件 | 自动迁移为 v3（§1.5） |
| `POST /api/config` 收到 v2 | 400 + 明确提示（不静默升级，避免旧页面覆盖新字段） |
| `GET /api/config` | 只返回 v3（磁盘 v2 会在读取时迁移后返回） |
| `/api/status` | 只增字段（`page`/`text2`/`cells`），旧字段含义不变 |
| 页面名 | 注册名仍为 `"TimerPage"`，`PageManager::getPage("TimerPage")`、`HttpServer` 的既有查找逻辑不受影响 |

---

## 2. 页面类型与渲染规则

### 2.0 坐标系与字体度量（实测，用于所有像素计算）

屏幕 52×16（LED 实际 52 列，SPI 帧补齐到 64 列，右侧 12 列恒黑）。
字体 `Fonts/TomThumb.h`：字形高 **5px**（数字/大写 `height=5`、`yOffset=-5`，即基线 `b` 时占据 `b-5 .. b-1` 行），
字宽按墨迹取（`TimerPage::glyphVisibleWidth`），**数字 3px 宽，字间距由调用方给**（`measureText` 用“墨迹宽 + spacing”）。

单字符 advance（spacing=1，scale=1）：

| 类 | 墨迹宽 | advance | 备注 |
|---|---|---|---|
| 数字 0-9 | 3 | 4 | |
| 大写/小写字母（除 I/N/Q/M/W） | 3 | 4 | |
| `I` `i` `l` | 1 | 2 | |
| `N` `Q` | 4 | 5 | |
| `M` `W` | 5 | 6 | |
| `:` | 1 | 2 | |
| `.` | 1 | 2 | |
| `-` | 3 | 4 | |
| 空格 | 0 | 3 | 不绘制 |

实测字符串宽度（`measureTomThumb(text, scale, spacing)`，与固件同一算法）：

| 文本 | scale | spacing | 宽度 | 用在哪 |
|---|---|---|---|---|
| `16:38:29` | 2 | 1 | **47** | clock 页主文本（自动收紧后 47 ≤ 52） |
| `--:--:--` | 2 | 1 | **47** | 时间无效占位 |
| `16:38:29` | 1 | 1 | 27 | clock_countdown 上半 |
| `2026-10-22` | 1 | 1 | 39 | clock_countdown 下半（无目标） |
| `4d04:05:06` | 1 | 1 | 35 | 远目标倒计时（分屏下半同理） |
| `1.2w` | 1 | 1 | 13 | 直播单元格值 |
| `9999` | 1 | 1 | 15 | 直播单元格值（4 位整数） |
| `B25` | 1 | 1 | 11 | 直播单元格标签 |
| `2/6` | 1 | 1 | 11 | 切页横幅页码 |
| `COUNTDOWN` | 1 | 1 | 39 | 切页横幅标题 |

> 说明：`drawScaledText` 的“先收紧 spacing、再按 52 截断”规则保留。真实时间只含数字和冒号，
> 因此 scale 2 时钟的自动收紧结果通常是 **spacing=1（47px）**，不是 0；`--:--:--` 同为 47px。

### 2.1 `clock` 页：全屏时钟（scale 2）

| 条件 | 文本 | 位置 | 颜色 |
|---|---|---|---|
| 时间有效 | 本地 `HH:MM:SS` | 水平居中，`top=(16-5*2)/2=3`，基线 13（占 3..12 行） | `colors.clock` |
| 时间无效 | `--:--:--` | 同上 | `#404040`（保持 v2 的硬编码占位灰） |

- 只有这一种版式，不看 `targets`；
- 3..12 行上下各 3px 留白，字高 10px；与 v2 `drawScaledText` 完全一致。

### 2.2 `clock_countdown` 页：上半时钟 + 下半倒计时（8px + 8px）

**几何（固定常量）**

| 常量 | 值 | 含义 |
|---|---|---|
| `kTopBaseline` | **7** | 上半文本基线：字形占 2..6 行 |
| `kBottomBaseline` | **14** | 下半文本基线：字形占 9..13 行 |
| 垂直节奏 | 2 + 5 + 2 + 5 + 2 = 16 | 上留白 2px、字高 5px、中间 2px、字高 5px、下留白 2px |

**为什么是 7/14**：两个 8px 半区里放 5px 字高，理论可用留白 3px/半区；取 2/2/2/2 的全局对称节奏
（而不是“每半区 1px 上、2px 下”）视觉最稳，也让 2×2 直播网格复用同一组基线。
字符高度 5px（数字/大写），无升部/降部；标签/数值统一用大写与数字，避免 `g/p/q/y` 的 1px 降部占用中间留白。

#### 2.2.1 备选几何：10+6（上 scale 2）——需与用户确认

并行文档 [`DESIGN-web-v3.md`](DESIGN-web-v3.md) 选了另一种分配：**上半 10 行用 scale 2 时钟、下半 6 行用 scale 1 倒计时**。
两种都物理可行，取舍不同：

| 方案 | 上半 | 下半 | 时钟宽度 | 留白 | 观感 |
|---|---|---|---|---|---|
| **8+8（本设计，需求原文）** | 5px 字形 = 2..6 行（基线 7） | 5px 字形 = 9..13 行（基线 14） | 27px（约全屏 47px 的 57%） | 2/2/2/2 对称 | 上下等权、留白舒服；时钟偏小 |
| 10+6 | 10px 字形 = 0..9 行（基线 10） | 5px 字形 = 10..14 行（基线 15） | 47px（与 clock 页同大） | 0/0/0/1 | 时钟醒目；上下贴边、无行距 |

- 8+8 的时钟在 52px 屏上只有 27px 宽，`16:38:29` 读起来偏小；
- 10+6 的时钟与时钟页完全一致（视觉统一），代价是两半之间 0 行距、上半顶到第 0 行；
- **本文件按需求原文采用 8+8**；若用户选 10+6，只需替换 §2.2 的常量（`kTopBaseline=10`、上半 scale=2、
  `kBottomBaseline=15`），渲染器其余逻辑不变（见 §10 Q13）。

**内容规则**

| 上半（时钟） | 下半（倒计时） | 说明 |
|---|---|---|
| 时间有效：本地 `HH:MM:SS`，scale 1，27px，居中 | 有未来目标：倒计时文本 scale 1（`HH:MM:SS` 27px；`≥100h` 用 `DdHH:MM:SS` 35px） | 两半都 **scale 1**，各自居中 |
| 时间无效：`--:--:--` scale 1，`#404040` | 时间无效：**留空** | 不猜日期 |
| — | 无未来目标：本地日期 `YYYY-MM-DD`（39px）`colors.dim`；或 `splitBottomWhenIdle="blank"` 时留空 | 这是“无目标时下半显示什么”的答案：**日期**（默认） |
| — | `remain ≤ warnSeconds`：`colors.warning`；否则 `colors.countdown` | 只变下半颜色，上半保持 `colors.clock` |

- 上下两半**必须**用同一个 `now` 计算（§8），否则会出现“上半 16:38:29、下半比它少 1 秒”的错位；
- 倒计时来源：全局 `targets` 里最近的一个未来目标（`TimerEngine::evaluate`），**不支持按页选目标**（见 §10 Q9）；
- 不做“上半 scale 2、下半 scale 1”的混排（scale 2 字高 10px > 8px 半区）。

### 2.3 `countdown` 页：沿用 v2 的“倒计时/时钟”逻辑

| 条件 | 文本 | 缩放 | 颜色 |
|---|---|---|---|
| 时间无效 | `--:--:--` | 2 | `#404040` |
| 无未来目标（且 `countdownWhenIdle="clock"`） | 本地 `HH:MM:SS` | 2 | `colors.clock` |
| 无未来目标（`countdownWhenIdle="blank"`） | 黑屏 | — | — |
| 有未来目标，`remain < 100h` | `HH:MM:SS` | 2 | `colors.countdown`；`remain ≤ warnSeconds` → `colors.warning` |
| 有未来目标，`remain ≥ 100h` | `DdHH:MM:SS`（`D` 不补零） | 1 | 同上 |

**何时显示时钟、何时显示倒计时**：完全由“当前时刻是否存在未来目标”决定，每秒重算；不存在“模式切换动画/计时器状态”。
这条与 v2 完全一致，也是迁移后屏幕不变的原因。

### 2.4 `live` 页：一页 4 个直播间

#### 2.4.1 先评估“4 行 × 4px”是否可行

**硬约束**：16 / 4 = **4px/行**，且行间**没有**任何余量（4×4=16）。TomThumb 字高 5px，
4 行需要 4×5=20px > 16px，**物理上放不下**。所以 4 行方案必须先做一个自定义 **3×4 点阵字体**（或 3×3）。

**3×4 所需的字形集**（若实现）：`0-9`(10)、`A-Z`(26)、`.` `:` `-` `/` `+` `%` `>`(7)、空格(1)，共 **44 个字形**。
位图 44×4 B = 176 B，度量表 44×6 B = 264 B，合计 ≈ 440 B（可接受）。

**3×4 数字的歧义（关键否决理由）**——下面是用 3 宽 × 4 高能画出的“唯一合理”形状：

```text
   0      5      6      8      9
  ###    ###    ###    ###    ###
  #.#    #..    #..    #.#    #.#
  #.#    ###    ###    ###    ###
  ###    ###    ###    ###    ..#
```

- `8` 与 `0` 只差第 3 行中间 1 个像素；`8` 与 `6` 只差第 2 行中间 1 个像素；`8` 与 `9` 只差第 4 行左边 1 个像素。
  在 1px 笔画 + 约 2.5mm 像素间距的 52×16 屏上，这 1 个像素的差别在正常观看距离（30..60cm）**不可靠**；
- `5` 在 3×4 里没有“上半封口 + 下半弯钩”的余量，只能退化成与 `6`/`8` 相近的形状；
- 4 行必须 **0 行距**：第 1 行的第 4 行像素与第 2 行的第 1 行像素在物理上相邻，配合逐行不同颜色，
  视觉上会“串行”；无法用 1px 分隔（加了就只剩 3px 字高，数字更不可读）。

**其他代价**：第二套字体意味着第二套渲染路径 + 第二套网页模拟器字形表 + 第二套测试矩阵；
且 `label`（平台字母）与 `value` 的基线/宽度规则都要重算。

**结论**：4 行 × 4px **不作为 v3 的默认实现**。它的唯一优势是每行可用 13 个字符（52/4px），
但代价是数字可读性，得不偿失。若将来确需 4 行密度，**建议**：新增 `layout:"rows4"` 开关 + 3×4 字体
（字形集如上，44 字形），并明确标注“紧凑模式、可读性下降”；本设计不实现（见 §10 Q10）。
本设计的 4 行“示例文本”仅作为该未来方案的规格留档：
`B2145 1.2w`（10 字符 = 39px）、`K88 8.9w`、`D07 1234`、`H66 --`，逐行按平台色 + 白色数值。

#### 2.4.2 备选 B：4 列 × 3 行（评估后不采用）

4 列 × 13px，每列 3 行 5px 文本（基线 5/10/15，15px 用满 16px），每行 3 字符：

```text
 B25  K3X  B07  D88      ← 第 1 行：平台字母 + 房间尾 2 位
 05   89   42   --       ← 第 2 行：房间号后段
 1.2w 8.9w 45   1.2      ← 第 3 行：指标值
```

- 容量最高（每房间 9 字符，标签与数值**同时可见**，全部 5px 字形）；
- 否决原因：① 13px 列宽只放得下 3 字符，长房间号必须拆行；② 3 行 5px 无行距（15/16），行与行相邻；
  ③ 阅读顺序变成“竖着读一列”，4 列并排时需要跨行配对，认知负担明显高于“一行一个房间”；
  ④ 列间没有空隙（13×4=52），只能靠颜色区分。
- 保留为文档化备选：若用户更看重“同时看到标签+数值”，这是唯一的 5px 方案。

#### 2.4.3 采用方案：2×2 网格（一页 4 个房间）

```text
x:   0 1            25 26 27           51
y  2 ┌───────────────┐  ┌───────────────┐
      │ B25      1.2w │  │ B07        45 │   ← 单元格 0 / 1，基线 7（字形 2..6 行）
y  6 └───────────────┘  └───────────────┘
y  7  （2px 空白，行 7、8）
y  9 ┌───────────────┐  ┌───────────────┐
      │ K3X      8.9w │  │ D88        -- │   ← 单元格 2 / 3，基线 14（字形 9..13 行）
y 13 └───────────────┘  └───────────────┘
```

| 常量 | 值 |
|---|---|
| 单元格数 | 4（固定，空槽留黑） |
| 单元格宽 | 26（x 0..25 / 26..51） |
| 单元格文本区 | 25px（左格 x=1..25，右格 x=27..51；x=0、x=26 为 1px 留白/分隔） |
| 上排基线 / 下排基线 | 7 / 14（与 §2.2 完全一致） |
| 标签对齐 | 文本区左对齐（两列标签成列） |
| 数值对齐 | 文本区右对齐（两列数值成列，便于比较大小） |
| 行间距 | 2px（行 7、8） |

**为什么 2×2 仍然满足“一页 4 个”**：4 个房间同时可见、同一刷新周期、同一屏；只是每格可用宽度
从 52px 降到 25px。容量换可读性：数字是 3×5 的标准 TomThumb，不是 4px 压缩字形。

#### 2.4.4 单元格内容与宽度预算

单元格文本 = `label` + `gap` + `value`，两段分别着色：

| 段 | 内容 | 颜色 | 宽度上限 |
|---|---|---|---|
| `label` | 平台字母 + 房间尾号（用户可改，1..3 字符，大写） | 平台默认色 / 房间 `color` | 17px（3 个 `M`/`W` 级宽字符） |
| `gap` | 2px（放不下退 1px） | 黑 | — |
| `value` | 指标值（§2.4.7 压缩格式，≤4 字符） | 白色 `colors.clock`；陈旧 `colors.dim`；无数据/错误见下表 | 15px |

实测预算（文本区 25px）：

| label | 宽度 | value | 宽度 | gap=2 | gap=1 | 结论 |
|---|---|---|---|---|---|---|
| `B25` | 11 | `1.2w` | 13 | 26 ✗ | **25 ✓** | 典型情况，gap 收到 1 |
| `B25` | 11 | `45` | 7 | 20 ✓ | 19 | 宽松 |
| `K3X` | 11 | `8.9w` | 13 | 26 ✗ | **25 ✓** | 同上 |
| `B25` | 11 | `1234` | 15 | 28 ✗ | 27 ✗ | label 必须缩到 2 字符：`B2`(7)+2+15=24 ✓ |
| `MMM` | 17 | `12.3w`* | 17 | 36 ✗ | 35 ✗ | label→2(11)+2+17=30 ✗ → label→1(5)+2+17=24 ✓ |

\* `12.3w` 不会被格式化器产出（最多 4 字符），这里只用于展示最坏边界。

**fit 阶梯（实现顺序，保证有界）**：

1. `gap=2`，label 原样；宽度 ≤25 → 采用；
2. `gap=1`；≤25 → 采用；
3. label 截到 2 字符（保留最左字符，通常就是平台字母），`gap=2`→ 仍超则 `gap=1`；
4. label 截到 1 字符，`gap=2` → 必然 ≤22，采用；
5. **不允许**再压缩 spacing、不允许截断 value（value 由格式化器保证 ≤4 字符 / ≤15px，截断数值会显示错数）。

> 因 value ≤15px、label 1 字符 ≤5px，第 4 步理论上界 5+2+15=22 ≤ 25，阶梯**必定收敛**。
> 网页编辑器用同一套表计算并提示“将被截断为 X”，用户可自行改短 label。

#### 2.4.5 每格示例文本（需求点：平台字母 / 房间尾号 / 指标值 / 单位）

4 个房间同时显示（一页 4 个直播间）：

```text
┌──────────────────────────────┐
│ B25      1.2w   B07       45 │   在线 1.2万 / 45 人
│ K3X      8.9w   D88       -- │   点赞 8.9万 / 抖音未配置
└──────────────────────────────┘
```

- 平台字母：`B`=bilibili、`K`=kuaishou、`D`=douyin、`H`=huya、`Y`=douyu、`C`=custom（颜色再区分一次）；
- 房间尾号：自动取房间号末 2 位（`B25`），可手工改成 `B2`/`B25X` 之外的 3 字符；
- 指标值：`1.2w`（1.2 万）、`45`、`999`、`8.9y`（8.9 亿）；
- 单位：由 `w`/`y` 后缀承载（万/亿），指标名由切页横幅与网页显示，不占屏幕宽度。

#### 2.4.6 颜色区分

| 平台 | 默认标签色 | 说明 |
|---|---|---|
| bilibili | `#00A1D6` | 蓝 |
| kuaishou | `#FF5A00` | 橙 |
| douyin | `#FF2D55` | 玫红 |
| huya | `#FFB300` | 琥珀 |
| douyu | `#FF6A00` | 橙红（与 kuaishou 接近，同页避免同用） |
| custom | `#FFFFFF` | 白 |

- 数值统一白色（`colors.clock`），保证“4 个数字”是同一视觉层级；
- 房间 `color` 可覆盖平台色；网页编辑器对同页颜色距离过近的组合给**警告**（不阻止）；
- 颜色只是辅助，字母标签才是主标识（对色盲用户也可用）。

#### 2.4.7 指标值压缩格式（有界，≤4 字符）

> 后缀二选一：本文件用 `w`/`y`（万/亿，中文习惯），并行文档 `DESIGN-web-v3.md` $5.5 用 `K/M/B`（ASCII 惯例）。
> **固件与网页必须一致**，合并时在 §11 #6 拍板；两者宽度都 ≤4 字符（≤15px），切换只改格式化器与测试表，布局不变。
> 下表以 `w`/`y` 为例。

| 值范围 | 规则 | 示例 | 字符数 | 宽度 |
|---|---|---|---|---|
| `v < 0` / 无数据 | `--` | `--` | 2 | 7 |
| `0 ≤ v ≤ 9999` | 十进制 | `0` `45` `999` `9999` | ≤4 | ≤15 |
| `10^4 ≤ v < 10^6` | 万：`<10万` 用 1 位小数，否则整数 | `1.2w` `9.9w` `10w` `99w` | ≤4 | ≤15 |
| `10^6 ≤ v < 10^7` | 万整数 | `100w` … `999w` | 4 | 15 |
| `10^7 ≤ v < 10^10` | 亿：`<10亿` 用 1 位小数，否则整数 | `0.1y` `1.2y` `12y` `999y` | ≤4 | ≤15 |
| `v ≥ 10^10` | 上限截断 | `999y` | 4 | 15 |

规则细节（实现时按此写测试）：
- 若 1 位小数格式进位到 2 位整数（如 `9.9999w → 10.0w`），改用整数格式 `10w`；
- 若整数格式进位到 4 位（如 `999.9999w → 1000w`，5 字符），**升级到下一个单位**（→ `0.1y`）；
- 小数位为 0 时省略（`5w` 而不是 `5.0w`）；
- `w`=万、`y`=亿，均为 ASCII，TomThumb 有字形；单位不单独占位。

#### 2.4.8 截断 / 横向滚动策略

- **数值永不截断、永不滚动**（显示错数比空着更糟）；由格式化器保证 ≤4 字符；
- **标签只截断不滚动**：按 §2.4.4 阶梯截到 1..3 字符；截断时保留最左字符（平台字母），确保平台仍可辨识；
- **v3 不做横向跑马灯**：LED 帧率 1Hz 稳态，跑马灯只能 1px/秒（不可用），提速到 5..10Hz 会让
  `sendLedData` 的固定开销（≥16ms/帧 + 3KB SPI）占到 8..16% CPU，且 4 格同时滚动视觉噪声大；
- 溢出时网页编辑器显示预测宽度和“将截断”提示，属于配置期问题，不在运行期滚动；
- 空槽（未配置/禁用）：整格留黑，不画 `--`（避免把“没配置”和“没数据”混淆）。

#### 2.4.9 数据状态 → 屏幕表现

| 状态 | 文本 | 颜色 |
|---|---|---|
| 未配置 / 房间 `enabled=false` | （留黑） | — |
| 等待首次响应 | `--` | `colors.dim` |
| 正常（`now - lastOk ≤ 3×refreshSec`） | 数值 | `colors.clock` |
| 陈旧（有旧值但超时） | 旧数值 | `colors.dim` |
| 失败且从未成功 | `--` | `colors.warning` |
| 时间无效 | 与直播无关，照常显示数值 | — |
| 整页 0 个启用房间 | `NO LIVE`（整屏居中，scale 1） | `#404040` |

整页数据源不可达（连接失败/`ok:false`）时，4 格一起进入“失败”或“陈旧”，另有
`/api/live/status` 给出 `lastError`；screen 上不再额外加图标（没有空间，也不该闪烁）。

### 2.5 切页横幅与覆盖层优先级

**优先级（高 → 低）**

| 级别 | 内容 | 触发 | 时长 | 版式 |
|---|---|---|---|---|
| 1 | IP / `NO WIFI` / `AP ...` / `AP OFF` 覆盖层 | 中键短按、WiFi 连上、SoftAP 就绪 | 4..10s（既有） | 既有两行小字（基线 6/14，scale 1，`renderOverlay` 不变） |
| 2 | 切页横幅 | 旋钮翻页 | **1200ms** | 两行：上行 `shortTitle`（ASCII；缺省=类型名，超宽截断），下行 `n/N`；基线 7/14，scale 1，居中，`colors.clock` |
| 3 | 页面内容 | — | — | §2.1–§2.4 |

- 覆盖层激活时**抑制**横幅（用户主动看 IP 时不该被切页横幅打断）；横幅激活时来覆盖层 → 覆盖层立即接管；
- 横幅期间整屏只画横幅（不叠内容），因为 52×16 没有“角标不影响主内容”的空间（scale 2 时钟已占满宽度）；
- 横幅文本示例：`CLOCK+CD`（31px）/ `2/6`（11px）；`COUNTDOWN` 39px 仍 ≤52；
- 横幅只在**页面真正改变**时出现（到边界不循环、或只有 1 页时不出现）。

### 2.6 无效时间 / 异常态在各页的统一表现

| 页面 | 时间无效 | 无未来目标 |
|---|---|---|
| `clock` | `--:--:--` scale 2 `#404040` | 同（本来就不看目标） |
| `clock_countdown` | 上半 `--:--:--` scale 1 `#404040`，下半留空 | 上半时钟、下半日期（`colors.dim`） |
| `countdown` | `--:--:--` scale 2 `#404040` | 时钟（默认）或黑屏 |
| `live` | 不受影响，照常显示直播数值 | 不受影响 |
| 横幅 / 覆盖层 | 照常工作（横幅不依赖时间） | — |

---

## 3. 旋钮 / 按键交互

### 3.1 事件 → 动作映射

`KeyManager` 实际发出的事件（见 `managers/KeyManager.cpp`）：旋钮是 `EV_ABS` 状态机的
“起点→终点”配对后发一次，`keyStatus` 恒为 0；按键是 `EV_KEY` 的 press(1)/release(0)。

| 事件 | keyCode | keyStatus | v2 行为 | v3 行为 |
|---|---|---|---|---|
| 顺时针一格 | `E_KEYCODE_CLOCKWISE`(0) | 0 | 未使用 | **下一页** |
| 逆时针一格 | `E_KEYCODE_ANTI_CLOCKWISE`(1) | 0 | 未使用 | **上一页** |
| 中键短按 | `E_KEYCODE_MIDDLE_BUTTON`(0x69) | 0 | 显示 `IP x.x.x.x` / `NO WIFI` 6s（SoftAP 时 `AP ...`） | **完全不变** |
| 中键长按 3s | 同上 | 1 起算 | 进入/退出 SoftAP，`AP ...`/`AP OFF` 覆盖层 | **完全不变** |
| 旋钮按键 | `E_KEYCODE_KNOB_BUTTON`(0x67) | 1/0 | 未使用 | **直播页：立即刷新数据**（`POST /api/live/refresh` 的本地等价）；非直播页：无操作 |
| 左键 / 右键 | `E_KEYCODE_LEFT_BUTTON`(0x6C) / `RIGHT`(0x6A) | 1/0 | 未使用 | 保留不分配（v3.1 再定），不注册回调以免误触 |

实现路径（不改 PageManager 接口）：

```text
KeyManager 回调（既有）
  ├─ 中键：既有短按/长按逻辑（原样保留）
  ├─ 旋钮/其它键：PageManager::getInstance().onKeyEvent(keyCode, keyStatus)
  │     └─ 当前页 = PageHost → PageHost::onKeyEvent()
  │           ├─ CLOCKWISE      → nextPage()
  │           ├─ ANTI_CLOCKWISE → prevPage()
  │           └─ KNOB_BUTTON    → requestLiveRefresh()
  └─ 返回（PageManager 的既有 mutex 内只做 O(1) 状态更新，不做 I/O、不做 SPI）
```

> `PageManager::onKeyEvent` 会持有其 `mMutex` 调用页面回调，因此 `PageHost::onKeyEvent`
> **只能改状态 + 置脏标记**，不能渲染、不能调 `PageManager`、不能做网络/存储 I/O。

### 3.2 翻页算法

```text
next/prev(delta)：
  n = pages.size()；n <= 1 → 忽略
  cur = 当前 index（越界先 clamp）
  for step = 1..n:
      idx = cur + delta*step
      if wrap: idx = (idx % n + n) % n
      else if idx < 0 or idx >= n: return false      // 到边界，静默
      if pages[idx].enabled: 切到 idx；return true
  return false
```

- **是否循环**：默认 `wrap=true`（4..12 页的设备，旋钮循环最自然）；可在网页关掉；
- 跳过 `enabled=false` 的页面（临时隐藏某页而不删除）；
- 只有 1 个启用页时旋钮无动作、不出横幅；
- 从第 1 页逆时针 → 最后一页（`wrap=true`）。

### 3.3 消抖与渲染节拍

- 旋钮不做软件消抖：`KeyManager` 已是“起点+终点”配对后发一次，一格一次事件；快速连转就是要连续翻页；
- 渲染用**脏标记**合并：连续 5 格只会在下一 tick 画最终页，不会排队画 5 帧；
- `onUI_Timer` 由 1000ms 改为 **250ms**：
  - 每 tick：`PageHost::tick()`（内部判断“秒变化 / 脏 / 横幅到期 / 覆盖层变化”，否则不画）；
  - 每 4 tick（=1s）：`TimeKeeper::tick()`、`ConfigMode::tick()`、`UiState::tick()`、延迟迁移检查；
  - 中键长按检测仍每 tick（250ms）检查（只读原子变量，开销可忽略）。
- 收益：切页/横幅反馈 ≤250ms；时钟最多滞后 250ms（v2 的 1Hz 定时器最多滞后 1s）；
- 成本：稳态仍是 1 帧/秒（秒变化才画）；`sendLedData` 每帧固定开销约 16..20ms（含 15ms 下限），250ms 轮询本身几乎零成本；
- 风险：FlyThings 定时器最小间隔需真机确认；若不支持 250ms，退到 500ms（横幅 ≥1200ms 仍能保证至少 1 帧）。

### 3.4 覆盖层与既有行为保持（逐条对照）

| 既有行为 | v3 是否变化 | 说明 |
|---|---|---|
| 中键短按显示 IP 6 秒 | 不变 | 仍走 `showOverlay("IP ...", 0x00FF00, 6000)`；SoftAP 时 `AP ...` 8s |
| `NO WIFI` 橙色 | 不变 | `0xFF8000` |
| 中键长按 3 秒进出 SoftAP | 不变 | 仍在 `onUI_Timer` 里用单调毫秒判断 |
| WiFi 首次连上显示 IP 6 秒 | 不变 | `WifiBootstrap::setOnConnected` 回调只写状态 |
| SoftAP 就绪显示热点 IP | 不变 | `ConfigMode::setOnReady` |
| 覆盖层计时用 `CLOCK_MONOTONIC` | 不变 | SNTP 跳时不会让提示提前消失 |
| 覆盖层整屏替换内容 | 不变 | 横幅优先级更低（§2.5） |
| 长按后松手不再显示 IP | 不变 | `sMiddleLongHandled` 逻辑保留 |

### 3.5 旋钮按键是否可用于其它操作

可以，且建议只用一个：

| 候选 | 评价 |
|---|---|
| **立即刷新直播数据**（推荐） | 只在直播页生效，语义直观，和“旋钮=翻页”不冲突；可顺带在刷新期间显示 1.2s 横幅 `REFRESH` |
| 跳到第 1 页 | 与“转很多圈回第 1 页”重复，价值低 |
| 进入配网页 | 与中键长按冲突，且误触成本高（断网） |
| 切换页面自动轮播开关 | 状态不可见，容易困惑 |

结论：v3 把旋钮按键固定为“直播刷新”；左/右键保留不分配（避免与未来硬件定义冲突）。

---

## 4. 当前页持久化策略（避免每次旋转写 flash）

### 4.1 存哪里，以及为什么不是配置文件

**决定**：当前页存 `StoragePreferences` 的独立键 `uiState`，**不写 `/data/multitimer.conf`**。

| 方案 | 问题 |
|---|---|
| 写进 `multitimer.conf` 的 `ui.current` | ① 每次旋转都要重写整份 ~5KB 配置 + `fsync` + `rename`；② 网页 POST 会整份覆盖，设备页位置与用户编辑互相踩；③ 配置文件变成“运行时状态”，`GET /api/config` 的语义被污染 |
| 每次旋转写 `StoragePreferences` | 仍是一次整份 preferences 重写（JFFS2），连续转 10 格 = 10 次写 |
| **`StoragePreferences` + 节流（采用）** | 旋转只改内存；稳定 30s 后才落盘，且两次落盘间隔 ≥120s |

`uiState` 值格式：`"<pageId>,<pageIndex>,<epochSec>"`，例如 `"p3,3,1792658309"`。
`pageId` 用于“页面顺序被编辑后仍能回到同一页”；`pageIndex` 是兜底；`epoch` 只用于日志/诊断。

### 4.2 写入节流（常量与算法）

```text
UiStatePolicy:
  kStableMs       = 30000     // 页面稳定 30s 才允许落盘
  kMinIntervalMs  = 120000    // 两次落盘至少间隔 120s

notePageChanged(index, nowMs):  mPending=index; mChangedMs=nowMs
shouldPersist(nowMs) -> bool:
   return mPending >= 0
       && nowMs - mChangedMs  >= kStableMs
       && nowMs - mLastWriteMs >= kMinIntervalMs
```

- `tick()` 每 1 秒调用一次判定（内部只做比较，不阻塞）；
- 落盘失败：保持 pending，下一轮（≥120s 后）重试，不每秒重试；
- 配置保存（`POST /api/config`）后：如果页面列表变化导致 index 被 clamp，**立即**写一次（此时用户本来就在改配置，多一次 preferences 写可忽略）；
- 额外触发：进入/退出 SoftAP 不写（与页位置无关）。

**最坏写入率**：每 120s 最多 1 次 → **30 次/小时**；真实使用（每天翻几次页）≈ **每天 1..5 次**。
对比：`TimeKeeper` 已经在每 300s 写一次 `clockState`（12 次/小时），本方案写入量不超过它的 3 倍，
且都发生在同一 preferences 文件上，JFFS2 的磨损均衡足以覆盖（按 30 次/小时持续一年 ≈ 26 万次，
分摊到多个擦除块后仍远低于 JFFS2 的块寿命；真实场景远低于此）。
每次落盘打一条 `LOGI_TRACE("UiState: persist page=%d id=%s")`，真机验收时按日志计数（§9.3）。

### 4.3 恢复顺序（重启 / 配置变更）

```text
restore():
  读 uiState → (id, index, epoch)                    // 运行时最后停留页
  若 pages 为空 → 用默认单页
  1) id 非空且能匹配到 pages[i].id 且该页 enabled     → 用它        （最后停留页，优先）
  2) index 在 [0, count) 且该页 enabled                → 用它        （id 丢失时的兜底）
  3) defaultPageId 能匹配到 enabled 页                 → 用它        （用户配置的开机页）
  4) 从 index 向后找第一个 enabled 页                  → 用它
  5) 第一个 enabled 页；都没有 → 0
```

- **优先级**：最后停留页 > `defaultPageId` > 第一页。这样“重启恢复当前页”和“用户设置开机页”两个需求都满足：
  用户设的开机页只在没有历史停留页（首次开机/清空 preferences）时生效；
- 如果用户希望“每次开机都回到默认页”，网页提供“清除记忆的当前页”按钮（`POST /api/page {"reset":true}` →
  删 `uiState` 键），文档里写明；
- 编辑页面顺序后：按 `id` 仍回到同一页（这是 `id` 存在的唯一理由）；
- 页面被删除：clamp，不报错；
- 重启且离线：页位置照样恢复（不依赖时间）；`epoch` 不参与判断；
- 恢复发生在注册页面之后、`navigateTo` 之前（§7.6 启动顺序）。

### 4.4 失败与异常

| 情况 | 行为 |
|---|---|
| `uiState` 键不存在/格式错 | 用第 0 个启用页，不报错（首次开机） |
| 落盘失败（flash 满/只读） | 记 warning，保持内存状态，按节流重试 |
| 掉电丢了最近 2 分钟内的页位置 | 可接受（文档明确写出“重启恢复到最近一次已落盘的页面”） |
| 只有 1 页 | 不写 `uiState`（没有意义） |

---

## 5. 直播数据刷新架构

### 5.1 分层与线程

```text
[网页/其它脚本] --HTTP--> [HttpServer 连接线程]
                                   │ POST /api/config（live 配置）
                                   ▼
                          LiveService::setConfig()   （只更新配置/房间表）
[bridge :8081] <--HTTP GET--  [LiveService 后台线程]  （1 个 detach 线程，阻塞 I/O + 超时）
                                   │ 写缓存（mutex）
                                   ▼
                          LiveCache（≤24 条房间记录）
                                   │ snapshotPage()（拷贝，短暂持锁）
                                   ▼
[UI 线程 250ms] PageHost::tick() → gatherFrame()（只取一次 time(NULL)）→ PageRenderer（纯函数）→ SPI
```

| 组件 | 线程 | 责任 | 禁止 |
|---|---|---|---|
| `LiveService` | 1 个后台线程（detach，进程生命周期） | 轮询、解析、写缓存、状态机 | 不画屏、不碰 `PageHost`/`Surface`/SPI、不写 flash |
| `LiveCache` | 被 LiveService 与 UI/HTTP 共享 | 房间状态、时间戳、陈旧判定 | 不做 I/O |
| `PageHost` | UI 线程（+ 按键/HTTP 线程只改状态） | 取快照、组帧、渲染、状态快照 | 不发起网络请求 |
| `HttpServer` 线程 | 每连接 1 个 | 读缓存出 `/api/live/status`、触发刷新 | 不阻塞等网络 |

**锁顺序（全局约定，避免死锁）**：`PageHost.mMutex` → `LiveService.mMutex` **不嵌套**；
实际做法是 `gatherFrame` 先在 `PageHost` 锁内拷出标量/页配置，**解锁后**再调 `LiveService::snapshotPage`，
最后无锁渲染。任何持有 `LiveService` 锁的地方都不回调 `PageHost`。

### 5.2 数据源模式与 bridge 契约

**事实修订（依据 [`RESEARCH-live-apis.md`](RESEARCH-live-apis.md)，2026-10-22 实测）**：

| 事实 | 对设计的影响 |
|---|---|
| 设备**有** OpenSSL 1.1.0i（`/lib/libssl.so.1.1` + `crypto`）与 `/res/ui/cacert.pem`（145 张 CA），官方 App 自己就链接它们 | 设备**并非**没有 TLS 能力；但 SDK/工具链**没有头文件、没有 libcurl**，要用只能手写声明 + 动态链接，工程风险高 |
| 整机 RAM 仅 ~35 MB（MemTotal 36,240 kB） | 大的 HTTPS 响应（抖音 440 KB）不可取；B 站 ~4 KB 可行 |
| 无用户 cookie 时：B 站 `getH5InfoByRoom` / `get_info` 可直连（HTTP 恒 200，必须看 `code`）；抖音无 `ttwid` 会被限流（200 + 0 字节）；快手需要 JS 计算的 `__NS_hxfalcon` 签名 | **B 站是唯一适合“设备直连”的平台**；抖音/快手必须由 bridge 承担 cookie/签名/风控 |

因此 v3 的设备端**默认只做明文 HTTP 客户端**，真实数据由局域网内的一台机器
（PC / 手机 / NAS / Home Assistant 自动化）提供；**B 站直连作为可选的 P4 模式**
（`source:"bilibili"`，复用 `HttpClient` 的 socket 层 + OpenSSL 动态链接，仅取 B 站，≤4 KB/房间，
`code != 0` 视为房间错误；是否实现见 §10 Q1）。模式：

| `live.source` | 行为 | 适用 |
|---|---|---|
| `off` | 不起线程，不发请求；直播页显示暗灰 `--` | 默认值、未配置 |
| `bridge` | 设备每 `refreshSec` 轮询 `url`，按页批量取 4 路 | 推荐：一次配置长期运行，三个平台都能覆盖 |
| `push` | 设备不轮询；外部脚本 `POST /api/live` 推数值 | 已有自动化/想完全控制刷新节奏 |
| `bilibili`（P4，可选） | 设备直连 `api.live.bilibili.com`（HTTPS + CA 校验），只支持 B 站 | 只有 B 站房间、不想跑 bridge 的用户 |

**bridge 请求**（设备 → bridge，GET，明文）：

```text
GET {url}?rooms=bilibili:21452505,bilibili:7734200,kuaishou:3x8k2m9,douyin:88776655
Host: <由 url 解析>
Accept: application/json
Connection: close
```

- 一次请求最多 4 个房间（当前页），逗号分隔，`platform:room`；
- 房间号字符集 `[A-Za-z0-9_-]`，因此不需要 URL 编码；
- 请求总长 ≤ 256 B，超时 `timeoutMs`。

**bridge 响应**（bridge → 设备，`Content-Type: application/json`）：

```json
{
  "ok": true,
  "ts": 1792658309,
  "rooms": [
    { "platform": "bilibili", "room": "21452505",
      "online": 12345, "likes": 678000, "gifts": 120, "fans": 45678, "status": 1 },
    { "platform": "kuaishou", "room": "3x8k2m9",
      "error": "room not found" }
  ]
}
```

规则：
- `ok:false`（可带 `error`）→ 整页失败，4 格按 §2.4.9 处理；
- 缺失的房间 / 房间项带 `error` / 指标字段缺失 → 该格无值（`--` 或保留旧值按陈旧处理）；
- 只接受数字指标；未知字段忽略；响应体 **≤4096 B**（超出截断并判失败）；
- 支持 `Content-Length` 和 `Transfer-Encoding: chunked`（设备端做最小 dechunk）；`Connection: close` 收完即断；
- HTTP 状态非 200 → 整页失败；
- **不支持 https**：`url` 以 `https://` 开头在配置校验阶段就 400。

**push 模式接口**（可选，同一缓存）：

```text
POST /api/live
{ "rooms": [ { "platform": "bilibili", "room": "21452505",
               "metrics": { "online": 12345, "likes": 678000 } } ] }
→ { "ok": true, "updated": 1, "ts": 1792658309 }
```

`push` 模式下缓存/渲染/陈旧规则与 bridge 完全一致，只是没有轮询线程。

### 5.3 缓存模型

```text
LiveRoomKey   = (platform, room)                     // 去重键，跨页共享
LiveRoomValue = { state, hasValue, value, updatedUtc,
                  lastAttemptUtc, consecutiveFailures, lastError }
LiveCache     = map<LiveRoomKey, LiveRoomValue>      // ≤ 24 条
```

| 状态 | 进入条件 | 影响渲染 |
|---|---|---|
| `EMPTY` | 未配置 / `enabled=false` | 留黑 |
| `WAIT` | 已配置、从未成功、无失败记录 | `--` dim |
| `OK` | 最近一次成功，且 `nowMs - lastOk ≤ 3×refreshSec` | 数值白 |
| `STALE` | 有旧值但超过 3 倍间隔 | 旧值 dim |
| `ERROR` | 从未成功且最近一次失败 | `--` warning |

- 缓存**不持久化**（重启后重新拉，避免每次刷新写 flash）；
- 房间被删除/页面被删除：`setConfig` 时回收对应条目；
- 同一房间出现在两页：共享一条缓存（只有当前页会触发刷新，另一页复用）。

### 5.4 刷新调度

| 项 | 规则 |
|---|---|
| 轮询范围 | **只轮询当前页的 4 路**；切页后立即（urgent）刷新新页，不等下一个周期 |
| 批量 | 一页 4 路 = **1 个 HTTP 请求**（bridge 批量返回），不是 4 个 |
| 间隔 | `live.refreshSec`（默认 30，15..300） |
| 超时 | `live.timeoutMs`（默认 3000，1000..5000）+ 连接超时 2s |
| 单飞 | 同时只允许 1 个在途请求；在途期间来的切页标记为 urgent，当前请求结束后立刻再发一次（合并，不排队多个） |
| 失败重试 | 不额外加速重试；下一个周期再试（避免风控/雪崩）；连续失败只记日志与计数 |
| 陈旧阈值 | `3 × refreshSec`（最短 60s） |
| 无网络 | WiFi 未连接时跳过本轮（不消耗超时），连接恢复后下一轮自动生效 |
| 页切换 | `PageHost` 解锁后调用 `LiveService::noteActivePage(index)` → 置 urgent + `condition_variable::notify` |
| 休眠实现 | `condition_variable::wait_for(到下次到期时间)`；收到 urgent 立即醒 |

时序示例（refreshSec=30）：

```text
t=0      UI: 切到直播页 → noteActivePage → urgent
t=0.1    Live: 发 GET（4 路）→ 200 → 写缓存（4 条 OK）
t=0.25   UI: 下一 tick 取快照 → 4 格显示数值
t=30     Live: 周期到 → 再发一次
t=60     bridge 挂了 → 失败：4 条保持旧值 → 90s 后判定 STALE（dim）
t=95     bridge 恢复 → 成功 → 回到 OK（白）
```

### 5.5 失败占位与诊断

| 现象 | 屏幕 | 诊断 |
|---|---|---|
| 未配置/`source=off` | 4 格 `--` dim | `GET /api/live/status` → `source.kind="off"` |
| 首次请求在途 | `--` dim | `state="wait"` |
| bridge 连不上 | `--` warning（从未成功） | `source.lastError="connect timeout"` |
| 曾经成功、现在失败 | 旧值 dim | `ageSec > 3×refreshSec` |
| 单房间错误（`error` 字段/缺项） | 该格 `--` warning 或旧值 dim | 房间项 `lastError` |
| 配置了 https | 配置校验 400，页面提示 | `POST /api/config` 返回错误 |

### 5.6 与 UI 线程解耦的硬性规则

1. UI 线程**只**做：持锁拷贝 → 渲染 → 发 SPI。任何 socket/文件操作都不在 UI 线程；
2. `PageRenderer` 是纯函数（输入 `PageFrame`，输出 `Surface`），无锁、无 I/O、无全局状态；
3. `LiveService` 的网络 I/O 期间**不持锁**：先取配置快照 → 解锁 → HTTP → 加锁写结果；
4. 状态快照（`PageStatus`）在渲染后一次性写入，HTTP 线程只读拷贝；
5. 直播数据的“时间”用 `CLOCK_MONOTONIC` 计算年龄，避免 SNTP 跳时把数据一下判成陈旧或永不过期；
   `updatedUtc` 只用于展示/日志。

### 5.7 资源预算

| 项 | 预算 |
|---|---|
| 线程 | +1（LiveService） |
| 缓存 | 24 × ~120 B ≈ 3 KB |
| 请求缓冲 | URL ≤256 B；响应 ≤4 KB |
| 请求频率 | 1 次 / refreshSec（30s → 2 次/分钟），单次 ≤4 房间 |
| 与 SPI 帧的关系 | 无直接关系；SPI 仍 1 帧/秒（脏时才多画） |

---

## 6. 配置与 API 变更

### 6.1 `GET /api/config`

```json
{
  "ok": true,
  "config": {},
  "device": {},
  "ui": {
    "pageIndex": 2,
    "pageCount": 6,
    "pageTitle": "COUNTDOWN",
    "pageType": "countdown"
  },
  "live": {
    "source": "bridge",
    "reachable": true,
    "lastOkUtc": 1792658309,
    "refreshSec": 30,
    "error": ""
  }
}
```

- `config` 的内容即 §1.1 的 v3 对象；`device` 与 v2 相同（另含 `apMode`/`apSsid`/`apIp`）；
- `ui` 是**运行时只读**信息（设备当前页），不在 `POST` 里接受；
- `live` 是数据源健康摘要，详细内容走 `/api/live/status`；
- 磁盘 v2 文件在读取时迁移，因此 `config.version` 总是 3。

### 6.2 `POST /api/config`

- 只接受 v3（`version` 缺省或 3，且必须有 `pages`）；v2 → **400**
  `{"ok":false,"error":"v2 config is not accepted, refresh the page"}`；
- 仍接受 `{"config":{...}}` 包裹（兼容既有网页写法）；
- 校验（§1.3）→ `ConfigStore::save()`（原子写）→ `applyCallback(config)` 热应用 → 返回
  `{"ok":true,"ui":{"pageIndex":..,"pageCount":..}}`；
- `applyCallback` 内只做：`PageHost::setConfig()` + `LiveService::setConfig()` + 页下标 clamp + 可选 `SAVED` 覆盖层（1.5s）；
  不做阻塞 I/O；
- 保存失败 → 500，不提交部分配置（沿用 v2）；
- 请求体 > 16 KB → 413；规范化 JSON > 12 KB → 413。

### 6.3 `GET /api/status`（增量）

在 v2 的 `state` 上**只增不改**：

```json
{
  "state": {
    "mode": "clock|countdown|invalid",
    "text": "16:38:29",
    "text2": "00:01:30",
    "remainSeconds": 90,
    "targetUtc": 0,
    "targetLocal": "",
    "warning": false,
    "smallFont": false,
    "overlay": false,
    "page": { "index": 1, "count": 6, "type": "clock_countdown", "title": "CLOCK+CD" },
    "banner": false,
    "cells": [
      { "label": "B25", "text": "1.2w", "state": "ok" },
      { "label": "B07", "text": "45",   "state": "ok" },
      { "label": "K3X", "text": "--",   "state": "error" },
      { "label": "D88", "text": "",     "state": "empty" }
    ]
  }
}
```

- `text`/`text2`：主/次文本（分屏页 = 上/下两半；live 页 `text` = `"LIVE"`、`text2` = 当前指标名）；
- `cells` 只在 live 页出现（其它页为空数组），供网页预览与排障；
- `mode` 语义保持“该页计时部分的状态”：`clock` 页永远 `clock`，live 页在时间有效时为 `clock`；
  页面类型以 `page.type` 为准。

### 6.4 `GET /api/live/status`

```json
{
  "ok": true,
  "source": {
    "kind": "bridge",
    "url": "http://192.168.31.10:8081/live",
    "reachable": true,
    "lastAttemptUtc": 1792658309,
    "lastOkUtc": 1792658309,
    "error": ""
  },
  "refreshSec": 30,
  "rooms": [
    { "page": 3, "slot": 0, "platform": "bilibili", "room": "21452505",
      "label": "B25", "state": "ok", "updatedUtc": 1792658309, "ageSec": 4,
      "metrics": { "online": 12345, "likes": 678 } },
    { "page": 3, "slot": 2, "platform": "kuaishou", "room": "3x8k2m9",
      "label": "K3X", "state": "error", "ageSec": 0, "error": "room not found" }
  ]
}
```

- 覆盖全部已配置房间（≤24），网页“直播状态”卡片用；
- 响应 ≤6 KB；无鉴权（与既有 API 同级，局域网内）。

### 6.5 `POST /api/live/refresh`

- 请求：`{"page": 3}`（可省略，默认当前页）；
- 动作：`LiveService::requestRefresh(page)` → 置 urgent + notify（不阻塞等待结果）；
- 返回：`{"ok":true,"queued":true,"page":3}`；越界 400；
- 用途：旋钮按键、网页“立即刷新”按钮、真机测试。

### 6.6 `POST /api/page`

- 请求：`{"index": 2}` 切页；或 `{"reset": true}` 删除 `uiState`（下次启动回 `defaultPageId`）；
- 动作：`PageHost::goToPage(2)`（显式指定就跳，不套 `enabled` 过滤）；触发切页横幅；按 §4 节流落盘；
- 返回：`{"ok":true,"index":2,"pageCount":6}`；越界 → 400；`reset` → `{"ok":true,"reset":true}`；
- 用途：网页“设为此页/上一页/下一页/清除当前页记忆”、自动化测试。

### 6.7 错误码与限制汇总

| 场景 | 状态码 | 说明 |
|---|---|---|
| JSON 语法错 / 结构错（pages/version/tz/colors） | 400 | 带 `error` 文本 |
| v2 配置 | 400 | `"v2 config is not accepted, refresh the page"` |
| 请求体 > 16 KB / 规范化 JSON > 12 KB | 413 | `"config too large"` |
| 页面数/房间数越界 | 400 | 明确上限 |
| 保存失败 | 500 | `"save config failed"` |
| 页面下标越界（/api/page） | 400 | |
| 连接数 > 8 | 503 | 既有 |

### 6.8 网页端（`web-src/index.html`）改造

> 网页的交互细节（表单布局、校验文案、可访问性、未保存提示、直播监控卡）以
> [`DESIGN-web-v3.md`](DESIGN-web-v3.md) §4/§7 为准；本节只列**固件/契约层必须对齐**的点。

保持“单文件、零外链、移动优先、深色”，卡片顺序调整为：

| # | 卡片 | v3 变化 |
|---|---|---|
| 1 | 时钟连接 | 不变 |
| 2 | 网络与对时（多 WiFi + SoftAP） | 不变 |
| 3 | 时区与显示 | 不变（`dim` 颜色不进 UI，用固定值） |
| 4 | **页面**（新） | 页面列表：序号 / 类型下拉 / 标题 / 启用 / ↑ ↓ ✕；新增页面（类型菜单）；最多 12；每页可展开 |
| 4.1 | 页面 · 直播设置 | 指标下拉（页面默认）、刷新间隔、4 个房间行（平台 / 房间号 / 标签 / 指标覆盖 / 颜色），每行显示“宽度 xx/25px、是否截断” |
| 4.2 | 页面 · 直播状态 | 调 `/api/live/status`：每房间 OK/`--`/ERR + 年龄；`refreshSec` 内自动轮询（≥5s 一次，页面隐藏时暂停）；“立即刷新”按钮 |
| 5 | 倒计时目标 | 不变（提示：只有 `countdown` / `clock_countdown` 页会用到） |
| 6 | 模拟器 | 升级：页面切换（上一页/下一页/页下拉）、按 4 种页面类型渲染、切页横幅预览、直播页用真实状态或本地 mock |
| 7 | 保存 | 不变（POST v3，成功后回读 + 静默对时） |

关键实现点：
- **模拟器要 1:1 还原固件字体**：把 TomThumb 的 `0x20..0x7E` 子集（95 字形 × 5 B = 475 B）以十六进制常量内嵌到页面，
  实现与固件**同算法**的 `measureTomThumb`/`drawTomThumb`（墨迹宽 + spacing，不用浏览器字体渲染文本）。
  否则 8px 分屏、25px 单元格的预览会“看起来能放下、真机放不下”。
- 模拟器复用固件常量：`kTopBaseline=7`、`kBottomBaseline=14`、单元格 26/25、fit 阶梯、值格式化；这些数字在
  `check_web.js` 里与固件测试用例共用同一组期望值；
- 页面编辑器的“宽度提示”确保用户不会保存一个必然截断的 label；
- 所有用户输入（title/label/room/url）只进 `textContent`/`value`，不拼 `innerHTML`（既有 XSS 规则）；
- 保存前校验与固件一致（§1.3），错误逐条列在状态日志；
- 设备返回 v2（旧固件）时：页面顶部提示“设备仍是 v2 固件，请升级/重新刷入”，页面编辑区禁用，其余卡片照常可用。

### 6.9 内嵌资源再生成

- 改完 `web-src/index.html` 后：`python native/MultiTimer/tools/build_web_asset.py` 重新生成
  `src/web/editor_gz.h`（构建产物，不手改），再 headless 构建；
- 页面体积预计 96 KB → 125..135 KB（TomThumb 表 + 页面编辑器），gzip 后约 +8..12 KB；
  `libzkgui.so` 与 `update.img` 增量同量级，8 MiB `/res` 预算无压力。

---

## 7. 文件级实现计划

### 7.1 新增 / 修改文件

| 文件 | 动作 | 职责 |
|---|---|---|
| `native/MultiTimer/src/pages/PageTypes.h` | 新增 | `PageType`/`LivePlatform`/`LiveMetric`/`LiveRoomConfig`/`PageConfig`/`DisplayConfig`/`LiveSourceConfig` + 上限常量（纯 C++11） |
| `native/MultiTimer/src/pages/PageRenderer.h/.cpp` | 新增 | 4 种页面的 52×16 绘制、切页横幅、覆盖层文本；纯函数，只依赖 `Surface` + `TimeMath` + `Stage`（主机可测） |
| `native/MultiTimer/src/pages/PageHost.h/.cpp` | 新增（由 `TimerPage.*` 重构） | 页表、当前页、翻页、横幅、覆盖层、状态快照、单帧时间源、渲染调度 |
| `native/MultiTimer/src/live/LiveTypes.h` | 新增 | 房间键/值/页快照/状态枚举（POD） |
| `native/MultiTimer/src/live/LiveCache.h/.cpp` | 新增 | 缓存、状态机、陈旧判定（纯 C++11，主机可测） |
| `native/MultiTimer/src/live/LiveFormat.h/.cpp` | 新增 | 指标值压缩（§2.4.7）、label 自动生成/规范化（纯，主机可测） |
| `native/MultiTimer/src/live/BridgeProtocol.h/.cpp` | 新增 | 构造 bridge 请求 path、解析响应（用 `mini_json`，纯，主机可测） |
| `native/MultiTimer/src/live/LiveService.h/.cpp` | 新增 | 后台线程、调度、HTTP 调用、push 更新、状态查询 |
| `native/MultiTimer/src/net/HttpClient.h/.cpp` | 新增 | 极简 HTTP/1.1 GET（POSIX socket，超时、4 KB 上限、chunked） |
| `native/MultiTimer/src/managers/UiStatePolicy.h/.cpp` | 新增 | 持久化节流判定（纯，注入单调时间，主机可测） |
| `native/MultiTimer/src/managers/UiState.h/.cpp` | 新增 | `StoragePreferences["uiState"]` 读写 + 恢复（平台层） |
| `native/MultiTimer/src/timer/TimerTypes.h` | 修改 | `TimerConfig` 增加 `colorDim`、`display`、`live`、`pages`（include `pages/PageTypes.h`） |
| `native/MultiTimer/src/config/ConfigStore.h/.cpp` | 修改 | v3 schema、v2→v3 迁移、页面/房间校验、JSON 字符串转义、12 KB 上限 |
| `native/MultiTimer/src/pages/TimerPage.h/.cpp` | 删除/改名 | 由 `PageHost.*` 取代（`git mv` 保留历史；注册名仍是 `"TimerPage"`） |
| `native/MultiTimer/src/pages/PageBase.cpp` | 小改（可选） | SPI 发送缓冲改为函数内 `static`，去掉每帧 `new[]/delete[]` |
| `native/MultiTimer/src/net/HttpServer.cpp` | 修改 | v3 校验路径、`/api/live/status`、`/api/live/refresh`、`/api/live`(push)、`/api/page`、16 KB 体限、`/api/status` 增量、`/api/config` 的 `ui`/`live` |
| `native/MultiTimer/src/logic/mainLogic.cc` | 修改 | 注册 `PageHost`、按键路由、`UiState::restore`、`LiveService::start/setConfig`、250ms tick + 分频 |
| `native/MultiTimer/web-src/index.html` | 修改 | 页面编辑器、直播设置/状态、模拟器（TomThumb + 4 类页面 + 横幅）、v3 校验 |
| `native/MultiTimer/tools/check_web.js` | 修改 | v3 自检（见 §9.2）；`REQUIRED_IDS` 扩到新 DOM |
| `native/MultiTimer/src/web/editor_gz.h` | 重新生成 | 构建产物 |
| `tests/test_config_store.cpp` | 修改 | v3 往返、迁移、页面/房间校验、上限、字符串转义 |
| `tests/test_page_renderer.cpp` | 新增 | 逐页像素/几何断言（§9.1） |
| `tests/test_live_format.cpp` | 新增 | 值压缩边界、label 自动生成 |
| `tests/test_live_cache.cpp` | 新增 | 状态机、陈旧阈值、页快照 |
| `tests/test_bridge_protocol.cpp` | 新增 | 请求 path、响应解析、错误路径 |
| `tests/test_ui_state_policy.cpp` | 新增 | 节流判定（假时钟：稳定 30s、间隔 120s、失败重试） |
| `tests/run_tests.ps1` | 修改 | 加入 `Surface.cpp`、`PageRenderer.cpp`、`LiveFormat.cpp`、`LiveCache.cpp`、`BridgeProtocol.cpp`、`UiStatePolicy.cpp` |
| `docs/DESIGN-pages-v3.md` | 本文件 | 设计契约 |
| `docs/HANDOFF-native.md`、`native/MultiTimer/README.md`、`docs/DESIGN-v2.md` | **不在本轮修改** | 实现完成后由文档负责人在新版本中更新（v2 契约冻结） |

### 7.2 关键接口签名（只给声明）

```cpp
// pages/PageTypes.h
enum PageType { PAGE_TYPE_CLOCK = 0, PAGE_TYPE_CLOCK_COUNTDOWN = 1,
                PAGE_TYPE_COUNTDOWN = 2, PAGE_TYPE_LIVE = 3 };
enum LivePlatform { LIVE_PLATFORM_BILIBILI = 0, LIVE_PLATFORM_KUAISHOU,
                    LIVE_PLATFORM_DOUYIN, LIVE_PLATFORM_HUYA,
                    LIVE_PLATFORM_DOUYU, LIVE_PLATFORM_CUSTOM };
enum LiveMetric { LIVE_METRIC_NONE = 0, LIVE_METRIC_ONLINE, LIVE_METRIC_LIKES,
                  LIVE_METRIC_GIFTS, LIVE_METRIC_FANS, LIVE_METRIC_STATUS, LIVE_METRIC_SCORE };

struct LiveRoomConfig {
    LivePlatform platform; std::string platformId;   // "bilibili"
    std::string room; std::string label; uint32_t color;
    LiveMetric metric; bool enabled;
};
struct PageConfig {
    std::string id; PageType type; std::string title;
    LiveMetric metric; std::vector<LiveRoomConfig> rooms; bool enabled;
};
struct DisplayConfig { bool wrap; bool countdownWhenIdleClock; bool splitBottomDate; };
struct LiveSourceConfig { int kind; std::string url; int refreshSec; int timeoutMs; };
```

```cpp
// pages/PageRenderer.h  —— 纯函数层（主机单测）
const int kScreenWidth = 52, kScreenHeight = 16;
const int kTopBaseline = 7, kBottomBaseline = 14;      // 5px 字形基线
int  tomThumbAdvance(char c, int scale);
int  measureTomThumb(const std::string& text, int scale, int spacing);
void drawTomThumb(Surface& s, const std::string& text, int x, int baseline,
                  int scale, int spacing, uint32_t rgb);
void drawTomThumbFitted(Surface& s, const std::string& text, int x, int width,
                        int baseline, uint32_t rgb);   // 自动收紧 spacing + 截断

struct PageFrame {
    const PageConfig* page;      // 非空
    const TimerConfig* config;   // 颜色/时区/warn
    Stage stage;                 // 调用方用同一个 nowUtc 求值
    long long nowUtc;
    bool timeValid;
    const LivePageSnapshot* live;  // 仅 live 页，可空
};
bool renderPage(const PageFrame& frame, Surface& surface);      // false = 未支持的页型
void renderPageBanner(int index, int count, const std::string& title,
                      const TimerConfig& config, Surface& surface);
void renderOverlayText(const std::string& text, uint32_t rgb, Surface& surface);
```

```cpp
// pages/PageHost.h  —— 注册名仍为 "TimerPage"
class PageHost : public PageBase {
public:
    PageHost();
    void setConfig(const TimerConfig& config);                 // 任意线程
    void showOverlay(const std::string& text, uint32_t rgb, uint32_t durationMs);
    void tick();                                               // UI 线程，250ms
    bool nextPage(); bool prevPage(); bool goToPage(int index); // 任意线程，只改状态
    int  currentPage() const;
    bool getStatus(PageStatus& out) const;                     // HTTP 线程
    void requestLiveRefresh();
    virtual void draw() override;
    virtual void onEnter() override;
    virtual void onExit() override;
    virtual bool onKeyEvent(int keyCode, int keyStatus) override;
};
```

```cpp
// live/LiveService.h  —— 平台层
class LiveService {
public:
    static LiveService& getInstance();
    void start();                                                   // 幂等
    void setConfig(const LiveSourceConfig& source,
                   const std::vector<std::vector<LiveRoomKey> >& pagesByIndex);
    void noteActivePage(int index);                                 // UI 线程
    void requestRefresh(int index);                                 // 任意线程
    bool snapshotPage(int index, LivePageSnapshot& out) const;
    bool statusJson(JsonValue& out) const;                          // /api/live/status
    bool push(const std::vector<LiveRoomUpdate>& updates);          // source=push
    void shutdown();
};
```

```cpp
// live/LiveCache.h / BridgeProtocol.h / net/HttpClient.h
class LiveCache {
public:
    void setRooms(const std::vector<LiveRoomKey>& rooms, int refreshSec);
    void noteAttempt(const LiveRoomKey&, long long monoMs);
    void applyValue(const LiveRoomKey&, long long value, long long utcSec);
    void applyFailure(const LiveRoomKey&, long long monoMs, const std::string& err);
    void snapshotPage(const std::vector<LiveRoomKey>&, long long monoMs,
                      LivePageSnapshot& out) const;
};
std::string buildBridgePath(const std::vector<LiveRoomKey>& rooms);   // "?rooms=..."
bool parseBridgeResponse(const std::string& body,
                         const std::vector<LiveRoomKey>& expected,
                         std::vector<LiveRoomUpdate>& out, std::string& err);
struct HttpGetResult { int status; std::string body; std::string error; };
bool httpGetPlain(const std::string& url, int timeoutMs, size_t maxBody,
                  HttpGetResult& out);
```

```cpp
// managers/UiStatePolicy.h（纯） / UiState.h（平台）
class UiStatePolicy {
public:
    void reset(int index, long long monoMs);
    void noteChanged(int index, long long monoMs);
    bool shouldPersist(long long monoMs, int& indexOut) const;
    void onPersisted(int index, long long monoMs);
};
class UiState {
public:
    static UiState& getInstance();
    void restore();                       // 读盘 + 解析 id/index
    int  pageIndex() const;
    void notePageChanged(int index);
    void tick(long long monoMs);          // UI 线程每秒
    void flush();
};
```

`PageStatus` 扩展（`pages/TimerPage.h` 的既有结构随文件迁移到 `PageHost.h`）：
`pageIndex`、`pageCount`、`pageType`、`pageTitle`、`text2`、`banner`、`live`（4 槽快照）。

### 7.3 线程与锁

| 组件 | 谁调用 | 锁 | 规则 |
|---|---|---|---|
| `PageHost` | UI tick / KeyManager 线程（翻页）/ HTTP（setConfig、status）/ WiFi 回调（overlay） | `mMutex` | 锁内只做数据交换；**不在锁内**渲染、SPI、网络、flash、调 `LiveService`/`UiState` |
| `LiveService` | 后台线程；UI（noteActivePage/requestRefresh）；HTTP（setConfig/status/push） | `mMutex` + `condition_variable` | 网络 I/O 不持锁；先取快照再发请求；结果回写时再取锁 |
| `UiState` | UI tick / HTTP（apply 后 flush） | `mMutex` | 只在 `PageHost` 解锁后调用 |
| `LiveCache` | 被 `LiveService` 与 `PageHost` 共享 | 由 `LiveService::mMutex` 统一保护 | 不单独加锁，避免双锁 |
| `ConfigStore` | HTTP | `gSaveMutex`（既有） | 不改 |
| SPI | UI 线程 | `PageBase::mSpiMutex`（既有） | 只在 UI 线程发帧；HTTP 线程永不发帧 |

启动顺序（`onUI_init` 增量，插在既有步骤之间）：

```text
1. 防砖属性 / MCU / TimeKeeper::restore()                    （既有）
2. 注册 PageHost（名 "TimerPage"）                            （替换 TimerPage）
3. UiState::restore()                                        （新增，必须在 setConfig 前）
4. ConfigStore::load()：v1→v2→v3 迁移 + 一次 save()            （改）
5. PageHost::setConfig() + 页下标 clamp                       （改）
6. LiveService::start(); LiveService::setConfig(config.live, 每页房间表)  （新增）
7. navigateTo("TimerPage")                                    （既有）
8. KeyManager 回调：中键既有逻辑 + 其它键转发 PageManager      （改）
9. Wifi/SoftAP 回调、HttpServer::start()                       （既有）
```

### 7.4 内存 / 栈 / 性能预算（C++11 + 32 位 ARM）

| 项 | 约束 / 做法 |
|---|---|
| 编译器 | FlyThings 工具链 GCC 8.3.0（`arm-pc-linux-gnueabihf`），工程 `-std=gnu++11`；**禁止** C++14/17 特性（`make_unique`、`string_view`、`optional`、结构化绑定、if-init） |
| 原子 | 不用 `std::atomic<long long>`（Z21 无 libatomic，v2 已踩坑）；时间戳用互斥锁保护，或 32 位 `atomic<unsigned long>` |
| 每帧分配 | **不要每帧拷贝整个 `TimerConfig`**（v3 配置可达 KB 级）：`gatherFrame` 只拷标量（颜色/tz/warn/display，<64 B）+ 当前 `PageConfig`（live 页最坏 ~300 B）+ `Stage` + 直播 4 槽快照 |
| Surface | 复用 1 个 `Surface` 成员（52×16×3 = 2.5 KB），每帧 `clear()`；避免 250ms..1s 一次的 `new[]` 抖动 |
| SPI 缓冲 | `sendLedData` 的 3 KB 暂存改 `static`（`mSpiMutex` 已串行化），去掉每帧 `new[]/delete[]` |
| 字符串 | `std::string` 短字符串为主；渲染循环里避免 `substr`/临时拼接；URL 与 JSON 用 `reserve` |
| 栈 | 后台线程默认栈即可；HTTP 客户端缓冲 ≤4 KB（堆上 `std::string`，不在栈上开大数组）；递归 JSON 深度受限（mini_json 既有实现） |
| 异常 | 不使用异常（`mini_json` 亦然）；所有失败走返回值 + 日志 |
| 时间 | `time_t` 32 位，2038 上限不变；页面/直播的“年龄”用 `CLOCK_MONOTONIC`（毫秒，64 位用两段 32 位或 `long long` 只在栈上，不进原子） |
| 闪存 | 配置只在保存时写；`uiState` 节流 ≤30 次/小时；缓存不落盘 |
| 内存总量 | 配置 ~6 KB + 缓存 ~3 KB + Surface 2.5 KB + 字符串峰值 < 20 KB，远小于可用堆；验收时记录 `free`/RSS |
| 可选小优化 | `sendLedData` 的 64 列补齐可以只做一次静态模板（12 列黑）；本轮不强制 |

### 7.5 构建与测试集成

| 命令 | 变化 |
|---|---|
| `python native\MultiTimer\tools\build_web_asset.py` | 无改动（读 `web-src/index.html` 生成 `editor_gz.h`） |
| `node native\MultiTimer\tools\check_web.js` | 新增 v3 断言（§9.2），`REQUIRED_IDS` 增加新 id |
| `powershell -File tests\run_tests.ps1` | 新增 6 个应用源文件 + 5 个测试文件；MSVC `/std:c++14` 与 g++ `-std=c++11` 都要通过 |
| `powershell -File tools\build.ps1 -Workspace build\v3-workspace -LogPath build\v3-build.log` | 并行构建约定不变（不要复用 v2 workspace/日志） |
| `tools\make-image.ps1` / `flash-update.ps1` | 不变（只换 `.so` 与 `main.ftu`） |

### 7.6 实施顺序与并行分工（建议）

| 阶段 | 内容 | 依赖 |
|---|---|---|
| S1 | `PageTypes.h` + `ConfigStore` v3 + 迁移 + 主机测试（core） | — |
| S2 | `PageRenderer` + 像素测试（core，可与 S1 并行） | S1 的类型 |
| S3 | `PageHost` + `mainLogic` 接线 + 按键路由 + `UiState`（device） | S1/S2 |
| S4 | `LiveFormat`/`LiveCache`/`BridgeProtocol` + 测试（core，可并行） | S1 的枚举 |
| S5 | `HttpClient` + `LiveService` + `HttpServer` API（device） | S3/S4 |
| S6 | 网页编辑器 + 模拟器 + 自检（web，可先做只读预览） | S1 的 schema、S5 的 API |
| S7 | 真机验收、文档/发布（qa/flash） | 全部 |

文件所有权建议沿用 v2 的划分：core（timer/pages 纯逻辑 + tests）、device（pages/live/net/logic）、
web（`web-src` + `check_web.js` + `editor_gz.h`）、flash（工具与镜像）、docs。

---

## 8. 与“倒计时严格绑定时钟”的保证

### 8.1 不变量（必须始终成立）

1. **每一帧只有一次时间采样**：`PageHost::gatherFrame()` 顶部 `now = time(NULL)`，本帧内所有文本
   （时钟、倒计时、日期、年龄）都用这同一个 `now`；
2. **剩余秒数 = 目标 epoch − now**，`TimerEngine::evaluate(now)` 每秒重算，**不存在**“剩余秒数缓存并递减”；
3. **同一帧内上/下两半不可能相差 1 秒**（分屏、直播页时钟都共享 `now`）；
4. 时间源只有系统时钟（`time(NULL)`）；`CLOCK_MONOTONIC` 只用于覆盖层/横幅/直播陈旧判定，
   **不得**用于倒计时或时钟显示；
5. 目标只存本地日期+墙上时间，`tzOffsetMinutes` 改变后下一次 `evaluate` 立即按新时区换算；
6. 时间无效（`now < 1700000000`）时一律显示 `--:--:--`，不显示历史剩余值。

### 8.2 每帧数据流

```text
tick(nowMs)                       // 250ms 进入
  now = time(NULL)                // ← 本帧唯一时间源
  lock(PageHost)
     拷贝：colors/tz/warn/display + 当前 PageConfig + 当前页下标 + 覆盖层状态
  unlock
  stage = mEngine.evaluate(now)   // 引擎内部用自己的配置副本（v2 既有）
  live  = (page.type==LIVE) ? LiveService::snapshotPage(index, nowMs) : 空
  renderPage({page, config, stage, now, timeValid, live}, surface)
  lock → mStatus = ...            // 供 /api/status
  sendLedData(surface)
```

- 若用“脏标记”跳过重绘，**只在“秒变化/切页/横幅/覆盖层/直播快照版本变化”时重绘**，
  但每个重绘帧仍然重新取 `now` 并重新 `evaluate`（不允许复用上一帧的 `stage`）；
- 覆盖层/横幅帧不显示时间，因此不需要 `now`，但依然不缓存任何剩余值。

### 8.3 禁止模式清单（代码评审必查）

| 禁止 | 原因 |
|---|---|
| `mRemainSeconds--` / `mLastRenderSec` 递减 | 校时/改时区后不会立即反映，违反需求 3 |
| 用 `CLOCK_MONOTONIC` 差值算倒计时 | 与系统时钟解耦，SNTP 校时后显示错误 |
| 首次进入页面时算一次 `remain` 存起来 | 同上 |
| 分屏页两半分别调用 `time(NULL)` | 可能跨秒，上下不一致 |
| 在 HTTP 线程/按键线程里渲染或发 SPI | 与 UI 线程竞争 Surface/SPI，且会阻塞 UI |
| 在 `PageRenderer` 里读全局时间 | 破坏可测试性（必须由 `PageFrame.nowUtc` 注入） |
| 缓存 `next target epoch` 跨帧 | 目标可能被 `POST /api/config` 热替换 |

### 8.4 测试（主机）

- 同一 `now` 下，`clock_countdown` 上/下文本由同一 `Stage` 推出（用注入 `now` 断言）；
- `now` 跳 ±1h / ±1 天：`countdown`、`clock_countdown` 的文本立即变化，无残留；
- 时区改动后重新 `evaluate`：文本变化；
- 跨帧不携带状态：连续两帧不同 `now` 的渲染结果只依赖输入（纯函数性质）。

---

## 9. 测试计划与风险

### 9.1 主机单测（`tests/run_tests.ps1`，必须全绿；现有 102 个用例不得回归）

**新增/扩展用例（按文件）**

`tests/test_config_store.cpp`（扩展）
1. v3 往返：`toJson` → `fromJson` 字段/顺序/默认值一致；
2. v2→v3 迁移：`version=2` + 2 个 targets + 自定义颜色 → 1 个 `countdown` 页、targets 原样、`colors.dim` 补默认；
3. v2 迁移后 `save()` 写回 v3，再 `load()` 不再迁移（幂等）；
4. v1→v2→v3 链式迁移（`points` 折算当天目标后仍生成 countdown 页）；
5. 页面校验：`pages` 非数组/空/13 项/未知 type → false；
6. 房间校验：0 项/5 项 → false；(platform, room) 重复 → 后者丢弃；room 25 字符 → 丢弃；
7. `enabled` 全 false → false；`title` 超 12 字符 → 截断；`label` 非法字符 → 置空；
8. `live.url`：`https://` → false；`http://` 正常；>160 字符 → false；
9. 全局直播房间 25 → false；
10. `version=4` → load 返回 false（保留原文件）；
11. JSON 字符串转义：`title` 含 `"`/`\`/控制字符时序列化仍可解析（v3 新引入字符串字段，v2 的 `toJson` 是手工拼接、没有转义）；
12. 规范化 JSON > 12 KB → 拒绝。

`tests/test_page_renderer.cpp`（新增，像素断言）
1. `clock`：时间有效时点亮的像素全在 3..12 行；文本宽度 47、起始 x=2；颜色 = `colors.clock`；
2. `clock` 无效：文本 `--:--:--`、颜色 `#404040`；
3. `clock_countdown`：上半点亮像素仅 2..6 行、下半仅 9..13 行；7、8 行全黑；上下文本分别是时钟/倒计时；
4. `clock_countdown` 无目标：下半 = 日期（39px）；`splitBottomWhenIdle=blank` 时下半全黑；
5. `clock_countdown` 预警：下半用 warning 色、上半仍是 clock 色；
6. `countdown`：`remain<100h` → scale 2（点亮行 3..12）；`≥100h` → scale 1（行 3..12 内的 5px 文本 + `DdHH:MM:SS`）；
7. `countdown` 无目标：与 v2 相同的 `HH:MM:SS`；
8. `live` 2×2：4 个格子的点亮像素分别落在 x∈[0,25]/[26,51]、y∈[2,6]/[9,13]；两列中点无像素交叉；
9. `live` 标签/数值颜色分段正确（平台色 vs 白色）；
10. `live` fit 阶梯：`B25`+`1.2w`（gap 1）、`B25`+`1234`（label 截 2）、`MMM`+`12.3w`（label 截 1）、任何组合 ≤25px；
11. `live` 无数据/错误：`--` dim / `--` warning；禁用房间整格黑；
12. 横幅：`n/N` + title 文本宽度 ≤52、基线 7/14、颜色；
13. 覆盖层文本截断 ≤52（沿用 v2 的两行规则）。

`tests/test_live_format.cpp`（新增）
- 值边界：`0/45/999/1000/9999/10000/99999/100000/9999999/10^7/10^8/10^10/负数/LLONG_MAX`；
- 进位规则：`9999w→` 升级；`10.0w→10w`；
- label 自动生成：`bilibili/21452505 → B25`；`kuaishou/abc → KAB`；全大写；
- 输出宽度 ≤15px（用 `measureTomThumb`）。

`tests/test_live_cache.cpp`（新增）
- `WAIT → OK → STALE → OK` 状态转换；`3×refreshSec` 阈值；单调时间注入；
- 24 条上限、房间删除回收、同房间跨页共享；
- `snapshotPage` 只返回该页 4 槽，缺项为 `EMPTY`/`WAIT`。

`tests/test_bridge_protocol.cpp`（新增）
- 请求 path：`?rooms=bilibili:1,bilibili:2`；空房间表；room 含 `_`/`-`；
- 响应解析：正常、缺房间、`ok:false`、非法 JSON、超 4KB、指标为字符串、负数、超大数。

`tests/test_ui_state_policy.cpp`（新增，假时钟）
- 变更后 <30s 不写；≥30s 且 ≥120s 写一次；连续变更多次只写最后一个；
- 写入失败后 pending 保留，下一轮 ≥120s 重试；
- index 未变不写；单页不写。

### 9.2 网页自检（`node native/MultiTimer/tools/check_web.js`）

现有 61 项检查保留，新增（编号顺延）：
1. 新 DOM id 齐全（页面列表/类型/标题/房间行/指标/刷新间隔/模拟器页选择/横幅开关/直播状态）；
2. `GET /api/config` 的 `ui` / `live` 渲染（当前页、页数、数据源状态）；
3. v3 保存请求体：`pages` 顺序、字段名、`version:3`、无 `ui` 字段；
4. 页面增删/上移/下移/启用；超 12 页禁用“添加”；
5. 直播房间校验：平台枚举、房间号长度/字符集、标签长度、指标覆盖、同页重复房间、总房间 ≤24；
6. 宽度提示：给定 label/value 时显示的预测宽度与固件一致（用同一张 TomThumb 表）；
7. 模拟器：4 种页面类型都能画（canvas 非空）、分屏上下两行、2×2 四格、live 用 `/api/live/status` 或 mock；
8. 模拟器页切换（上一页/下一页/页下拉）与横幅预览；
9. 设备返回 v2 时的降级提示（不崩、页面编辑禁用）；
10. 零外链、无 `https` fetch、无 `innerHTML` 拼用户数据、title/label 的 XSS 尝试被转义；
11. 直播状态轮询间隔 ≥5s，页面隐藏（`visibilitychange`）时暂停——避免手机页把设备打满。

### 9.3 真机验收（`/tmp` 与固化后各一遍）

| # | 步骤 | 判据 |
|---|---|---|
| 1 | 刷入含 v2 配置的设备，`GET /api/config` | 返回 v3，`pages` 只有 1 个 `countdown`，targets/tz/colors/warn 原样；屏幕与 v2 一致 |
| 2 | 网页添加 4 类页面并保存 | 设备热应用；旋钮顺序 = 网页顺序 |
| 3 | 顺时针转 6 次 / 逆时针转 6 次 | 页序正确；`wrap` 打开时首尾相接；每格一次翻页；横幅 `n/N + title` 出现约 1.2s |
| 4 | 快速连转 10 格 | 不卡顿、不跳页、只画最终页；无 SPI 错误日志 |
| 5 | `clock_countdown` 页（需求 6） | 上半时钟、下半倒计时；行 7/8 无亮点；到点变红；无目标时下半显示日期 |
| 6 | `countdown` 页 | 有目标倒计时、无目标时钟；`≥100h` 小号 `DdHH:MM:SS` |
| 7 | 严格绑定 | 在分屏页 `POST /api/time` 前后跳 1 小时：上下两半**同时**变化，差值始终一致 |
| 8 | `POST /api/time` 改时区 480→330 | 目标本地时间与剩余秒数立即按新时区 |
| 9 | 直播页（bridge 已配） | 4 格数值 ≤1s 内刷新；bridge 断开 90s 后变 dim/`--`；恢复后回白 |
| 10 | 直播页（`source=off`） | 4 格 `--` dim，无请求产生（抓包/日志） |
| 11 | 旋钮按键 | 直播页立即刷新；非直播页无副作用 |
| 12 | 当前页恢复 | 停在 3 页，稳定 >30s 后断电重启 → 回到 3 页；`/data/preferences.json` 里 `uiState` 与日志计数一致 |
| 13 | flash 写入频率 | 连续翻页 2 分钟：`UiState: persist` 日志 ≤2 次（120s 节流） |
| 14 | 中键短按 / 长按 | IP 覆盖层 6s；长按 3s 进 SoftAP、`AP ...`；松手不再显示 IP；退出后恢复当前页（横幅不重播） |
| 15 | 网页 | 390×1200 视口无横向溢出、0 console error、保存/回读/模拟器/直播状态正常 |
| 16 | 大配置 | 12 页 + 24 房间 + 12 目标保存成功；超过上限返回 400/413 且有明确文案 |
| 17 | 稳定性 | 连续 24h：帧率、RSS、线程数、SPI 错误、HTTP 可用性 |

验证命令（示例）：

```powershell
curl.exe http://<IP>:8080/api/config
curl.exe http://<IP>:8080/api/status
curl.exe http://<IP>:8080/api/live/status
curl.exe -X POST http://<IP>:8080/api/page -H "Content-Type: application/json" -d '{"index":2}'
adb -s <IP>:5555 shell "tail -f /dev/null"   # 或 logcat 过滤 UiState/PageHost/LiveService
adb -s <IP>:5555 shell "cat /data/preferences.json"
```

### 9.4 风险登记表

| # | 风险 | 等级 | 触发 | 缓解 | 验证 |
|---|---|---|---|---|---|
| R1 | **平台接口是 HTTPS + 风控，设备侧只有 socket/无 libcurl/无头文件** | 高 | 直接连抖音/快手，或 B 站直连实现不当 | 默认走 bridge/push（明文 HTTP）；`https://` 在配置校验阶段 400；B 站直连只作为 P4（复用设备 OpenSSL 1.1.0i，`code!=0` 视为房间错误，≤4 KB/次，注意 35 MB RAM） | [`RESEARCH-live-apis.md`](RESEARCH-live-apis.md) §0.4 + §9.3-9/10 |
| R2 | 4 房间可读性（每格 25px ≈ 6..7 字符） | 中 | 房间号/标签过长 | fit 阶梯 + 标签 ≤3 字符 + 数值压缩 + 编辑器宽度提示；4 行方案已否决（§2.4.1） | 像素测试 + 目视 |
| R3 | 当前页持久化磨损 flash | 低 | 高频翻页 | 30s 稳定 + 120s 间隔；日志计数；失败重试不加速 | §9.3-13 |
| R4 | 250ms 定时器不受支持/渲染开销 | 低 | 平台定时器粒度 | 脏标记；不支持则退 500ms；稳态仍 1 帧/秒 | 真机帧/日志 |
| R5 | v3 配置变大（体限/内存） | 中 | 12 页 24 房间 | 体限 16 KB、规范化 12 KB、页面/房间上限；每帧不整份拷贝 | 主机用例 + 真机 free |
| R6 | 直播轮询线程阻塞/泄漏 | 中 | bridge 不响应/DNS 慢 | 超时、单飞、condvar 唤醒、无网络跳过；不持锁 I/O | 断网/慢网实验 |
| R7 | 页面顺序被编辑后当前页错位 | 低 | 网页重排 | `pages[].id` + 恢复顺序（§4.3） | 用例 + 真机 |
| R8 | 网页单文件继续膨胀 | 中 | 编辑器/模拟器 | 纯函数化、复用固件常量、`check_web.js` 静态检查 | 自检 |
| R9 | 字符串字段引入 JSON 转义 bug | 中 | 标题/标签含引号 | `ConfigStore` 增加转义 + 往返用例；标签字符集限制 | 用例 11 |
| R10 | 分屏页两半跨秒 | 低 | 两次取时 | 单帧单 `now`（§8） | 像素/单元用例 |
| R11 | 32 位 `time_t` 2038 | 低 | 长期 | 沿用 v2 限制与文档 | 文档 |
| R12 | 覆盖层/横幅优先级回归 | 低 | 中键 | 表格化优先级 + 真机 14 | §9.3-14 |
| R13 | 4 行模式被要求追加 | 中 | 用户改主意 | 本文 §2.4.1 已给字形集与代价；作为 v3.1 选项 | 评审 |

### 9.5 回滚

- 代码级：v3 只新增文件 + 改 `ConfigStore/HttpServer/mainLogic/web`；`git revert` 即可回到 v2；
- 配置级：v3 写回的 `multitimer.conf` 是 v3，旧固件（v2）读到 `version:3` 会**拒绝加载**并使用默认空配置
  （屏幕回到时钟）——因此**回滚固件前需先备份 `/data/multitimer.conf`**，或先手工改回 v2 结构；
  这一条要写进发布说明；
- 设备级：沿用 v2 的 `/res` 固化回滚流程（复位键恢复官方会清空 `/data`）。

---

## 10. 未决问题（需要用户拍板）

| # | 问题 | 现状/建议 | 影响 |
|---|---|---|---|
| Q1 | 直播数据到底由谁提供？ | 依据 [`RESEARCH-live-apis.md`](RESEARCH-live-apis.md)：**B 站可设备直连**（无 cookie/签名，~4 KB），抖音需 `ttwid` 且限流，快手需 JS 签名。建议 v3 先做 `bridge` + `push`（覆盖 3 平台），**B 站直连作为 P4 可选**；bridge 由用户在 PC/NAS/HA 上跑 | 决定是否要实现 `HttpClient`/轮询线程、是否引入 OpenSSL 动态链接；决定交付物是否包含 bridge 示例脚本 |
| Q2 | 首批平台与指标 | 建议 bilibili / douyin / kuaishou 三平台 + `online`/`likes`/`fans`/`status`；`huya`/`douyu`/`gifts`/`score` 为扩展位（bridge 有就显示）。平台能力以 bridge 实际返回为准 | 决定枚举与默认色表 |
| Q3 | 指标是“每页一个”还是“每房间可选” | 本设计**允许** `rooms[].metric` 覆盖页面 `metric`（默认取页面值）；UI 上页面指标是主选择 | 若要求严格“每页一个”，可去掉覆盖字段，校验更简单 |
| Q4 | 上限 | 建议 12 页、每直播页 4 房间、全局 24 房间、每页 4 房间固定 | 影响体限、内存、轮询预算 |
| Q5 | 当前页恢复的新鲜度 vs flash 寿命 | 建议“稳定 30s + 间隔 120s”（掉电最多丢 2 分钟页位置，最多 30 次写/小时）。若更在意恢复精度，可改 15s/60s | 影响 §4.2 常量 |
| Q6 | 旋钮到两端是否循环 | 建议默认循环（`display.wrap=true`），网页可关 | 影响 §3.2 |
| Q7 | 旋钮按键（0x67）用途 | 建议 = 立即刷新直播数据；左/右键保留不分配 | 影响 §3.1 |
| Q8 | `clock_countdown` 无目标时下半显示什么 | 建议日期 `YYYY-MM-DD`（39px，dim 色）；备选：留空；不建议显示“下一个目标”（此时没有） | 影响 §2.2 |
| Q9 | 倒计时是否允许“按页绑定某个目标” | 本设计沿用“全局最近未来目标”（需求：倒计时仍来自现有指定时间目标）；若要多页分别倒计时不同目标，需要新增 `pages[].targetIndex` | 影响 schema 与引擎接口 |
| Q10 | 是否需要 4 行紧凑模式 | 本设计否决（数字歧义）；如坚持要，作为 v3.1 的 `layout:"rows4"` + 3×4 字体 | 影响工作量与风险 |
| Q11 | 标题/标签是否接受纯 ASCII | `title` 可为中文但**不上屏**；上屏的 `shortTitle` 与 `label` 必须是 ASCII（TomThumb 无中文字形） | 影响网页输入限制与提示文案 |
| Q12 | 是否需要“页面自动轮播（dwell）” | 当前不做（旋钮手动翻页）；如需，加 `display.dwellSec` 与自动翻页定时器 | 影响 §3 与配置 |
| Q13 | 分屏页几何：**8+8/双 scale 1**（本设计，需求原文）还是 **10+6/上 scale 2**（并行文档 `DESIGN-web-v3.md` §3.2/§5.2 的选择）？ | 8+8 上下等分、留白对称，但时钟只有 27px 宽（约为全屏时钟的 55%）；10+6 时钟与时钟页同大（47px），下半 6 行只能贴底（基线 15，行 10..14，0 行距）。两者都可行，**需拍板**；若选 10+6，本文件 §2.2 的常量整段替换（基线 10/15，上半 scale 2） | 影响 §2.2 全部几何、像素测试、模拟器 |
| Q14 | v3 schema 以哪份文档为准？ | 本文件与 [`DESIGN-web-v3.md`](DESIGN-web-v3.md) 并行产出，字段名/上限/格式有差异（见 §11 对照表）。建议合并为一份契约后再实现，避免固件/网页各写一套 | 影响实现顺序：schema 冻结是第一步 |

---

## 11. 与并行设计文档的对齐（合并清单）

本轮同时产出了两份 v3 设计稿：

- 本文件：**固件/显示架构**（页面模型、渲染几何、翻页、持久化、刷新线程、API、文件计划）；
- [`DESIGN-web-v3.md`](DESIGN-web-v3.md)：**网页配置器/模拟器**（编辑器 UX、保存校验、模拟器、API 契约）。

两份文档在“2×2 直播布局、4 行不可行、请求体 16 KB、当前页不写配置、`/api/page`、`/api/live/status`、
ASCII 上屏、预览必须用 TomThumb 位图”这些大方向上**一致**；下面是需要合并拍板的差异（**建议**列是本文立场）：

| # | 项 | 本文件 | `DESIGN-web-v3.md` | 建议 |
|---|---|---|---|---|
| 1 | 分屏页类型名 | `clock_countdown` | `split` | 合并时取一个；建议 `split`（短、与“分屏”一致），本文件其余内容不受影响 |
| 2 | 分屏几何 | 8+8，两半 scale 1（基线 7/14） | 10+6，上 scale 2（基线 10/15） | **需用户拍板**（Q13）；默认按需求原文 8+8 |
| 3 | `countdown` 页无目标 | 显示时钟（v2 行为，迁移后屏幕不变） | `NO TARGET` | 建议保留 v2 行为（`countdownWhenIdle` 可配），否则升级后老用户看不到倒计时 |
| 4 | 房间字段 | `rooms[].room` / `label` / `metric` / `color` | `rows[].roomId` / `metric`（无 label） | 合并为 `rooms[]` + `roomId`（更明确）+ 可选 `label`；两者都支持“页级 metric + 房间覆盖” |
| 5 | 直播单元文本 | `label` + `value`（`B25 1.2w`） | `TAG` + `value`（`B1.2K`） | 二者同构（TAG/label 都是 1..3 字符）；合并后统一为 `label`（自动生成 `B25`） |
| 6 | 数值后缀 | `w`/`y`（万/亿） | `K`/`M`/`B` | 必须二选一（否则同一数值固件与网页显示不同）；建议 `K/M/B`（ASCII 惯例、与网页监控一致），`w/y` 作为中文偏好可后续加配置 |
| 7 | 页数/房间上限 | 12 页 / 24 房间 | 8 页 / 20 房间 | 取 8 页 / 20 房间（更保守、省 RAM）或 12/24；**建议 8 页 / 20 房间**，体限 16 KB 不变 |
| 8 | 全局设置命名 | `display{wrap,...}` + `live{source,url,refreshSec,timeoutMs}` | `defaultPageId` + `liveRefreshSeconds` | 合并：`defaultPageId` + `display{wrap}` + `live{source,url,refreshSec,timeoutMs}`；`liveRefreshSeconds` 作为 `live.refreshSec` 的别名接受（读取时兼容，写出只写嵌套） |
| 9 | 标题 | `title`（中文，网页用）+ `shortTitle`（ASCII，上屏） | `title`（中文，不上屏） | 采用“`title` + `shortTitle`”：既允许中文命名，又能让切页横幅显示 ASCII 短名 |
| 10 | `status` 指标 | 支持（`OFF`/`LIVE`） | 支持（`TAG` + `LIVE`） | 一致，保留 |
| 11 | `fans` vs `followers` | `fans` | `fans` | 一致（本文已从 `followers` 改为 `fans`） |
| 12 | 迁移默认页 | 1 页 `countdown` | 3 页（clock/split/countdown） | **建议 1 页**（见 §1.5 论证：升级后屏幕形态不变）；3 页方案会让老用户开机看不到倒计时 |
| 13 | 空直播页/无启用行 | 4 格留黑 | `NO LIVE` 整屏 | 建议 `NO LIVE`（用户知道“配置了但没启用”比黑屏好）；本文件 §2.4.9 增加该状态 |
| 14 | 页面 id 规则 | `[a-z0-9_-]{1,8}` | `^[a-z][a-z0-9]{0,7}$` | 取交集：`^[a-z][a-z0-9_-]{0,7}$` |

> 合并动作建议：先冻结 **schema（字段名/上限/数值格式）**，再各自实现；schema 冻结前不要写 `ConfigStore`
> 与网页表单，否则两边都要返工。`RESEARCH-live-apis.md` 是数据源事实依据，实现 P4 前需重新实测（平台接口会变）。

---

## 附录 A：页面类型速查

| 类型 | 屏幕形态 | 数据来源 | 每秒重算 |
|---|---|---|---|
| `clock` | 全屏 `HH:MM:SS`（scale 2） | 系统时钟 + tz | 是 |
| `clock_countdown` | 上 `HH:MM:SS` / 下倒计时或日期（scale 1，基线 7/14） | 系统时钟 + 全局 targets | 是（同一 `now`） |
| `countdown` | 全屏倒计时，无目标回时钟（同 v2） | 全局 targets | 是 |
| `live` | 2×2 × 4 房间（scale 1，基线 7/14） | `LiveService` 缓存 | 数值按刷新周期；时钟无关 |

## 附录 B：验收用「一页 4 房间」像素样例

```text
52 列宽（0..51），16 行高（0..15）；"#" = 点亮
行 0-1   : 空
行 2-6   : B25 左对齐 x=1..11 ；1.2w 右对齐 x=13..25 ；B07 左对齐 x=27..37 ；45 右对齐 x=45..51
行 7-8   : 空（分隔）
行 9-13  : K3X 左对齐 x=1..11 ；8.9w 右对齐 x=13..25 ；D88 左对齐 x=27..37 ；-- 右对齐 x=45..51
行 14-15 : 空
```

（上例的 label 宽 11px、值宽 13/7px，gap=1/2，均 ≤25px，属 §2.4.4 的典型情况。）
