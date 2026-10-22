# TC002 MultiTimer v2 设计契约（时钟 + 带日期多目标倒计时）

> 状态：**已实现并冻结**。所有 Agent 必须按本文件实现，不得自行改字段名、语义或显示规则。
> **实现/真机验证状态：已完成（2026-10-22）。**
> 上游依据：`docs/STATE-2026-10-22.md`、`docs/HANDOFF-native.md`、
> `docs/RESEARCH-tc002-wifi-deploy.md`、官方 `doc.md` 与
> `%TEMP%\uclock-sdk\IDE使用说明\说明文档.md`。
> 所有时间格式（界面、接口、配置）一律 `HH:MM:SS`，含秒；日期一律 `YYYY-MM-DD`。

## 1. 需求

1. **正常时钟**：联网后 SNTP 校时；校时成功后即使断网也继续走时；网页端可配网、可对时。
   当前机内 WiFi（`JasonYiL`）保持不变，部署/刷机不得清 `/data`。
2. **带日期的多目标倒计时**：每个目标包含 `YYYY-MM-DD` + `HH:MM:SS`，可多个；
   到达第一个目标后自动倒计时到下一个，依次进行；配置掉电保存。
3. **倒计时严格绑定时钟**：每一秒都用“目标时刻 − 当前系统时钟”重新计算；
   校时/改时区后立即反映，不使用独立计数器。
4. **时区可调**：支持任意分钟偏移（-720..+840，含 +05:30、+05:45、+12:45 等）。

## 2. 语义

- **时间源**：设备系统时钟（UTC epoch）。`TimeKeeper` 统一封装。
- **时间有效**：`now >= 1700000000`（2023-11-14）。无效时屏幕显示 `--:--:--`。
- **本地时间**：`local = utc + tzOffsetMinutes * 60`；设备无 tzdata、无 RTC、`sysclktz 0`。
- **时区**：只存分钟偏移，不自动处理夏令时；界面提供常用时区名列表 + 自定义分钟。
- **目标**：存本地日期时间（不存 epoch）。因此改时区后，目标按新时区的同一“墙上时间”解释；
  这是“时钟功能的一部分”，与需求 3 一致。
- **到期/过期**：目标 epoch < now 视为已过期，跳过；epoch == now 时显示 `00:00:00`；
  下一秒自动切到下一个目标；没有未来目标时切回时钟。
- **排序**：启用目标按解析后的 epoch 升序，相同 epoch 去重（保留配置中靠前者）。
- **模式**：有任何未来目标 → 倒计时；没有 → 时钟；时间无效 → 无效占位。
  不做“正计时”，不保留 countup 阶段。
- **校时/离线**：SNTP 成功立即写系统时钟并持久化；运行中每 5 分钟把当前 epoch 写入
  `/data`（`StoragePreferences` 键 `clockState`，官方 API）。重启后若系统时间无效，
  用最后保存的 epoch 恢复（无 RTC，断电期间的时间无法得知，只能恢复到最后保存时刻）。
- **严格绑定**：`TimerPage` 每秒取 `time(NULL)` 重新 `evaluate()`；禁止缓存剩余秒数递减。

## 3. 配置 schema（`/data/multitimer.conf`）

```json
{
  "version": 2,
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
  ]
}
```

校验：

| 字段 | 规则 |
|---|---|
| `version` | 可缺省；存在时必须为 2 |
| `tzOffsetMinutes` | 整数 -720..840（必填） |
| `warnSeconds` | 整数 0..600（缺省 10） |
| `colors.clock/countdown/warning` | `#RRGGBB`；缺省 `#FFFFFF/#00FF00/#FF2020` |
| `targets` | 数组，最多 **12** 项；每项 `date` 合法日历日 2000-01-01..2099-12-31，`time` 合法 `HH:MM:SS`，`enabled` 布尔（缺省 true） |
| 非法单项 | 丢弃该 target，不报错 |
| 超过 12 项 | 整体拒绝 |
| 未知字段 | 忽略 |

**v1 → v2 迁移**（仅 `ConfigStore::load` 从磁盘读取时执行）：

- `tzOffset`（小时，-12..14）→ `tzOffsetMinutes = tzOffset * 60`；
- `points`（最多 8 个 `HH:MM`）→ 转换成本地日期 = **当前本地日期**（由传入的 `nowUtc`
  按迁移后的时区折算）的 `targets`，时间补 `:00`；不推断“明天”，已过点的自然过期；
- 无法确定当前日期（`nowUtc < 1700000000`）时不迁移，目标为空，保留原文件；
- 迁移完成后由 `mainLogic` 立即 `save()` 为 v2（只写一次）。
- `POST /api/config` **不接受** v1 schema，返回 400 并提示刷新页面。

## 4. 显示规则（52×16，TomThumb）

| 模式 | 条件 | 文本 | 颜色 | 缩放 |
|---|---|---|---|---|
| 无效 | 时间未同步 | `--:--:--` | `#404040` | 2 |
| 时钟 | 无未来目标 | 本地 `HH:MM:SS` | `colors.clock` | 2 |
| 倒计时 | 有未来目标且剩余 < 100h | `HH:MM:SS` | `colors.countdown`；剩余 ≤ `warnSeconds` 时 `colors.warning` | 2 |
| 倒计时（远） | 剩余 ≥ 100h | `DdHH:MM:SS`（如 `4d04:05:06`） | 同上 | 1 |
| 覆盖层 | IP / NO WIFI | 原有两行小字逻辑 | 不变 | 1 |

- 所有可见时间都带秒；最长时间 `DdHH:MM:SS`，`D` 不补零，`HH/MM/SS` 补两位。
- 预警常亮红色，不闪烁；`warnSeconds=0` 关闭预警。
- 屏幕宽度不足时 `drawScaledText` 自动收紧字距；仍不足则截断（极长天数）。
- 中键覆盖层行为保持：显示 `IP x.x.x.x` / `NO WIFI` 6 秒（`CLOCK_MONOTONIC` 计时）。

## 5. C++ 结构

新增/重写（其余文件不动）：

```
src/timer/TimerTypes.h     配置数据结构（新）
src/timer/TimeMath.h/.cpp  日历/时区/格式化（新，纯 C++11，可 PC 单测）
src/timer/TimerEngine.h/.cpp 目标解析与阶段计算（重写）
src/config/ConfigStore.h/.cpp JSON v2 + v1 迁移（重写）
src/time/TimeKeeper.h/.cpp  系统时钟、持久化、恢复、对时记录（新，平台层）
src/pages/TimerPage.h/.cpp  渲染 + 状态快照（重写）
src/net/HttpServer.h/.cpp   API v2（重写）
src/net/WifiManager_task.*  周期 SNTP 重试（改）
src/net/SntpClient.*        保持接口，仅必要修正（改）
src/logic/mainLogic.cc      接线（改）
web-src/index.html          配置页 v2（重写）
native/MultiTimer/tools/check_web.js  网页自检（重写）
```

接口以已冻结的头文件为准；实现者不得改签名。纯逻辑模块禁止包含 FlyThings/平台头。

### 5.1 TimerEngine

```cpp
Stage evaluate(long long nowEpoch) const;
// STAGE_CLOCK：无未来目标
// STAGE_COUNTDOWN：最近目标；remainSeconds=max(0,target-now)
// warning = warnSeconds>0 && remain<=warnSeconds
// smallFont = remain >= 100h
```

### 5.2 TimeKeeper

- `restore()`：启动最早阶段调用（MCU 初始化后、页面注册前）。
- `isValid()`、`now()`、`setUtc(epoch)`、`noteSynced(epoch)`、`lastSyncUtc()`、
  `tick()`（每秒调用，内部 300 秒节流持久化）、`saveState()`。
- 持久化用官方 `StoragePreferences::putString("clockState", "epoch,lastSync")`。
- 不要用 `std::atomic<long long>`（Z21 无 libatomic），需要时用互斥锁。

### 5.3 HTTP API

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/`、`/index.html` | 内嵌手机页 |
| GET | `/api/config` | `{ok, config, device}` |
| POST | `/api/config` | v2 schema → 保存 → 热应用；也接受 `{"config":{...}}` 包裹 |
| POST | `/api/time` | `{"epoch": 1700000000..4102444800}`；立即写时钟并持久化 |
| POST | `/api/wifi` | 不变 |
| GET | `/api/status` | 实时状态（见下） |
| OPTIONS/`/favicon.ico` | | 不变 |

`device`：

```json
{
  "ip": "192.168.31.124",
  "ssid": "JasonYiL",
  "wifi": true,
  "uptimeSec": 715,
  "timeUtc": 1792658309,
  "timeValid": true,
  "tzOffsetMinutes": 480,
  "localTime": "2026-10-22 16:38:29",
  "lastSyncUtc": 1792658309
}
```

`/api/status`：

```json
{
  "ok": true,
  "device": { "...同上..." },
  "state": {
    "mode": "clock|countdown|invalid",
    "text": "16:38:29",
    "remainSeconds": 0,
    "targetUtc": 0,
    "targetLocal": "",
    "warning": false,
    "smallFont": false
  }
}
```

## 6. 网页端（`web-src/index.html`）

单文件、零外链、移动优先，保持现有深色风格。卡片顺序：

1. **时钟连接**：地址、设备信息（IP/SSID/运行时长/设备本地时间 `YYYY-MM-DD HH:MM:SS`/时区/时间是否有效/最近对时）。
2. **网络与对时**：当前 WiFi/IP；WiFi 名称+密码（预填当前 SSID，密码只提交不保存）；
   保存并重连；立即对时。不得主动修改现有 WiFi。
3. **时区与显示**：时区下拉（常用 UTC-12..UTC+14，含半小时/45 分钟）+ 自定义分钟输入；
   显示计算出的 `UTC±HH:MM`；时钟颜色、倒计时颜色、预警颜色；预警秒数。
4. **倒计时目标**：每行 启用开关 / `<input type="date">` / `<input type="time" step="1">` /
   本地备注（仅浏览器 localStorage）/ 上移 / 下移 / 删除；
   显示“已过期 / 进行中 / 等待中”徽标；添加（最多 12）；“移除已过期”。
   时间输入必须显示秒（`step="1"`），提交为 `HH:MM:SS`。
   日期和时间分开输入；空值高亮报错。
5. **52×16 模拟器**：按 v2 规则预览时钟/倒计时/预警/远目标；跟随真实时间或手动日期+时间（带秒）；
   显示当前模式文本。
6. **保存**：POST v2 配置；成功后重新加载；再静默对时一次。
7. **状态日志**：保留现有风格，错误完整展示。

行为：

- 首次加载 `GET /api/config`；设备时间与浏览器差 > 2 秒自动 `POST /api/time`。
- 保存前校验：`tzOffsetMinutes`、颜色、`warnSeconds`、目标日期/时间/上限。
- 不把 WiFi 密码、备注写进设备；备注只进 localStorage。
- 页面内部时间换算必须用 `tzOffsetMinutes`（不是小时）。

## 7. 限值与错误

- 目标上限 12；时区 -720..840；预警 0..600；目标年份 2000..2099。
- 请求体上限 8 KB；目标列表 JSON 远小于该值。
- 无效 JSON/字段返回 400 + `{"ok":false,"error":"..."}`。
- 保存失败 500；设备时间未同步时 `/api/time` 仍可用（范围校验内）。

## 8. 测试与验收

### 主机单测（`tests/run_tests.ps1`，必须全绿）

- TimeMath：闰年、月末、2000/2100、epoch 往返、负时区、分钟偏移（+330/+345/+765）、
  `formatCountdown`（99:59:59 / 100h / 天格式）、`formatEpochLocalTime`。
- TimerEngine：空目标 → CLOCK；单目标；多目标顺序；过期跳过；恰好在目标；
  预警边界；目标跨日/跨月/跨年；时区变化重算；重复去重；禁用忽略；远目标 smallFont。
- ConfigStore：v2 往返；v1 迁移（含 now、tz、日期）；非法日期/时间/颜色/范围；
  12 项上限；非法单项丢弃；v1 `points` 迁移。

### 网页自检（`node native/MultiTimer/tools/check_web.js`）

- 脚本可编译；模拟引擎与固件规则一致；schema 往返；日期+秒处理；时区分钟；
  IPv4/错误路径；零外链。

### 真机验收（`/tmp` 与固化后各一遍）

1. `GET /api/config` 返回 v2；旧配置迁移为当天目标。
2. 设置 now+90s 目标 → `/api/status` 显示倒计时；到 0 后切下一个；最后切时钟。
3. `POST /api/time` 前后跳 1 小时 → 剩余秒数立即变化（严格绑定）。
4. `tzOffsetMinutes` 480→330 → 时钟与目标本地时间按新时区显示。
5. 断电/重启后（固化版）配置、WiFi、时间恢复；离线仍走时。
6. 网页可保存、预填 SSID、对时、模拟器正常；手机浏览器 0 JS 错误。

## 9. 文件所有权（并行开发约定）

| Agent | 独占文件 |
|---|---|
| core | `src/timer/TimerTypes.h`、`TimeMath.*`、`TimerEngine.*`、`src/config/ConfigStore.*`、`tests/test_time_math.cpp`、`tests/test_timer_engine.cpp`、`tests/test_config_store.cpp`、`tests/run_tests.ps1` |
| device | `src/time/TimeKeeper.*`、`src/pages/TimerPage.*`、`src/net/HttpServer.*`、`src/net/WifiManager_task.*`、`src/net/SntpClient.*`、`src/logic/mainLogic.cc` |
| web | `web-src/index.html`、`native/MultiTimer/tools/check_web.js`、`src/web/editor_gz.h`（生成） |
| flash | `tools/make-image.ps1`、`tools/flash-update.ps1`、`docs/FLASH-RUNBOOK.md`、`dist/`（构建产物） |
| docs/QA | 根 `README.md`、`native/MultiTimer/README.md`、`docs/*.md`（除本文件与 FLASH-RUNBOOK） |

公共头文件（`TimerTypes.h`、`TimeMath.h`、`TimerEngine.h`、`ConfigStore.h`、
`TimeKeeper.h`、`TimerPage.h`）由协调者冻结；实现者只能改自己名下的 `.cpp`
（`core` 可在实现中补私有成员，但不得改公共签名）。

## 10. 明确不做

- 不做每日循环点（旧 `points` 仅迁移一次为当天目标）。
- 不做正计时（COUNTUP）、不做响铃、不做闪烁。
- 不做自动夏令时（无 tzdata；界面提示手动调整）。
- 不做 OTA/远程升级、不做 MQTT、不做鉴权。
- 不修改官方固件以外的分区；刷机只替换 `/res`，`/data` 保留。
