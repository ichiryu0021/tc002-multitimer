# MultiTimer v3.3 纯 C++ 模块 · PC 单元测试

本目录为 `native/MultiTimer/src` 下**不依赖 FlyThings SDK** 的纯 C++ 模块提供可在
Windows 上直接运行的单元测试。测试**直接编译真实的应用源文件**（不复制、不改动），
仅新增测试代码与脚本；不修改设备构建行为。

## 1. 结论（最近一次运行）

| 项目 | 结果 |
| --- | --- |
| 宿主编译器 | MSVC 19.44.35228（x64，VS 2022 BuildTools，工具集 14.44.35207） |
| 编译选项 | `/std:c++14 /EHsc /W4 /utf-8`，`/DMULTITIMER_HOST_CONFIG_DIR=tests/out/data` |
| 用例总数 | **211** |
| 通过 | **211** |
| 失败 | **0** |
| 结果 | `Summary: 211 cases | passed 211 | failed 0` / `RESULT: ALL TESTS PASSED` |
| 退出码 | `0` |
| 运行时间 | 2026-10-22 22:02:00（本机，UTC+8；v3.3 源码） |
| 输出样例 | `tests/out/` 为生成物目录 |

> 本目录未修改 `native/MultiTimer` 下任何文件。上述数字对应第 6 节的源码快照；
> 源码变更后请重跑 `run_tests.ps1`。v3.0 QA 报告（构建 0 errors / 单测 209/209 /
> 网页 90/90）见 [`../docs/QA-v3-initial.md`](../docs/QA-v3-initial.md)，
> 最终回归见 [`../docs/QA-v3-final.md`](../docs/QA-v3-final.md)
> （独立重编与 Lead 产物逐字节一致；均为 **v3.0 基线**）。
> v3.3 已于 2026-10-22 21:49:28 生成镜像并固化真机（回读 `/res/lib/libzkgui.so`
> = 722,532 字节 / `1A495991...`），主机单测 211/211、网页 90/90；
> v3.3 暂无独立 QA 报告，不影响本套主机测试的结论与数字。
> `tests/out/v21_run.log` 是 v2.1.2 时期（102 用例）的旧输出，
> `tests/out/last_run.log` 是 v2 时期（92 用例）的旧输出，
> **不要**再把它们当作当前结果。

## 2. 测试对象

| 模块 | 编译进测试的真实源文件 | 说明 |
| --- | --- | --- |
| `TimeMath` | `src/timer/TimeMath.cpp`、`TimeMath.h` | 纯 C++11：日历换算、分钟时区、`YYYY-MM-DD` / `HH:MM:SS` 解析与格式化、倒计时格式 |
| `TimerEngine` | `src/timer/TimerEngine.cpp`、`TimerEngine.h` | 依赖 `timer/TimerTypes.h`；按 UTC epoch 评估时钟/倒计时/正计时阶段 |
| `ConfigStore` | `src/config/ConfigStore.cpp`、`ConfigStore.h` | **v3** JSON 严格校验、原子保存、v1/v2→v3 迁移；主机测试通过宏重定向配置目录，真实执行 `load()/save()` |
| `mini_json` | `src/net/mini_json.cpp`、`mini_json.h` | 纯标准库 |
| `WifiStore` | `src/config/WifiStore.cpp`、`WifiStore.h` | INI/JSON 单凭据解析 + v2 网络列表（≤8、旧单对象兼容、去重） |
| `BilibiliParse` / `BilibiliClient` | `src/net/BilibiliParse.cpp`、`BilibiliClient.*`、`TlsClient.h`（仅声明） | B 站房间号校验、h5/`get_info` 响应解析、错误映射、HTTP 响应/chunked 解码（**不发起真实网络/TLS**） |
| `PageRenderer` | `src/pages/PageRenderer.cpp`、`PageRenderer.h`、`src/live/LiveFormat.cpp`、`src/live/LiveTypes.h` | v3 页面像素级渲染：clock / clock_countdown / countdown / live / **datetime**、正计时/NO COUNTDOWN、横幅 / 覆盖层 |
| `LiveFormat` | `src/live/LiveFormat.cpp`、`LiveTypes.h`、`pages/PageTypes.h` | 指标格式化（w/y）、status、平台色、label、指标枚举 |
| `Surface` | `src/utils/Surface.cpp` | 帧缓冲（绘制测试共用） |

### 关于 `MULTITIMER_HOST_CONFIG_DIR`

`ConfigStore` 在设备上固定使用 `/data/multitimer.conf`。PC 上没有 `/data`，因此
`tests/run_tests.ps1` 在编译时定义：

```text
MULTITIMER_HOST_CONFIG_DIR=tests/out/data
```

`ConfigStore.cpp` 通过这个宏把配置目录重定向到 `tests/out/data`，从而在主机上真实测试
`load()/save()`、原子替换和 v1/v2→v3 迁移。**设备构建不定义该宏**，路径仍是 `/data`。
脚本在运行测试前会 `Push-Location` 到仓库根目录，保证测试代码与应用代码解析到同一个
相对目录；该路径刻意不含空格。

### 关于 `tests/shim/unistd.h`

`ConfigStore.cpp` / `WifiStore.cpp` 保存路径使用 `fsync(fileno(fp))` 并
`#include <unistd.h>`，而 MSVC 没有该 POSIX 头。因此测试构建额外加了一个
**仅主机测试用**的 `tests/shim/unistd.h`（把 `fsync` 映射到 `_commit`），并通过
`/I tests\shim` 引入。该 shim 不进入设备构建，也不修改应用源码；
g++/clang++ 分支不加此目录，使用系统 `<unistd.h>`。

## 3. 文件列表

| 文件 | 作用 |
| --- | --- |
| `test_framework.h` | 极简断言框架：`TF_TEST`、`TF_CHECK`、`TF_CHECK_MSG`、`TF_EQ`；失败即中止当前用例并记录 `文件:行号` |
| `test_main.cpp` | 用例注册表与 runner，输出 PASS/FAIL 与汇总，返回失败用例数 |
| `test_time_math.cpp` | **24 个用例**：闰年、月末、公历/epoch 往返、分钟时区、解析/格式化、倒计时边界 |
| `test_timer_engine.cpp` | **19 个用例**：目标排序/去重/过期跳过、时钟/倒计时/**正计时**阶段、预警、小号字、跨日/跨年、时区变化 |
| `test_config_store.cpp` | **54 个用例**：v3 字段/页面/直播/颜色/上下限、v3 round-trip、真实 `load/save`、v1/v2→v3 迁移、`ntpServer` 校验 |
| `test_mini_json.cpp` | **10 个用例**：解析/类型/转义/嵌套/dump round-trip/非法输入/深嵌套 |
| `test_wifi_store.cpp` | **19 个用例**：INI 与 JSON 单凭据解析（13）+ v2 网络列表（6） |
| `test_page_renderer.cpp` | **44 个用例**：5 类页面像素/几何/颜色、分屏同秒、**正计时/NO COUNTDOWN**、**datetime**、live 两行/占位/格式、fit 阶梯、横幅、覆盖层、越界安全 |
| `test_live_format.cpp` | **24 个用例**：数值压缩（w/y）、status、平台色/label、指标选择、枚举 round-trip |
| `test_bilibili_client.cpp` | **17 个用例**：房间号校验、h5/`get_info` 解析（0 值有效、错误码、缺字段）、HTTP chunked/Content-Length/裸 LF/截断/上限 |
| `shim/unistd.h` | MSVC 主机测试专用的 POSIX 头垫片（见第 2 节） |
| `run_tests.ps1` | 自动定位编译器、编译真实源文件 + 测试、运行并透传退出码 |
| `out/` | 生成物（`.obj`、`multitimer_tests.exe`、`data/`），可随时删除 |
| `visual/`（如存在） | v2 时期可视化/网页测试的产物，与本套 C++ 单元测试无关，本脚本不会读取或修改 |

> 本 README 只描述 C++ 单元测试这一套；`tests/` 下其他子目录属于历史测试任务。
> 网页自检是另一条命令：`node native/MultiTimer/tools/check_web.js`（当前 **90/90**）；
> bridge 的 Python 单测在 `tools/live-bridge`（**40/40**，见
> [`../docs/VERIFY-bridge.md`](../docs/VERIFY-bridge.md)）。

## 4. 运行方式

在仓库根目录 `D:\Project\ulanzi` 执行（推荐，兼容 Windows PowerShell 5.1 与 PowerShell 7）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1
```

也可以：

```powershell
tests\run_tests.ps1                 # 需要允许本地脚本执行
tests\run_tests.ps1 -Toolchain msvc # 强制 MSVC
tests\run_tests.ps1 -Toolchain gcc  # 强制 g++/clang++
```

脚本行为：

1. 优先用 PATH 里的 `cl.exe`；否则用 `vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64` 找到 VS，调用 `VC\Auxiliary\Build\vcvars64.bat` 导入环境，再定位 `cl.exe`。
2. 没有 MSVC 时回退 `g++` / `clang++`（`-std=c++11 -Wall -Wextra -O1`）。
3. 都找不到时报 `No host C++ compiler found`，不会伪造结果。
4. 生成物写入 `tests/out/`（不写 `build/`），测试进程退出码 = 失败用例数（0 = 全通过）。

手工等价的 MSVC 命令（在 `tests\out` 已存在时，仓库根目录运行）：

```bat
cl /nologo /std:c++14 /EHsc /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DPROGMEM= ^
   /DMULTITIMER_HOST_CONFIG_DIR=tests/out/data ^
   /I tests\shim /I native\MultiTimer\src ^
   /Fo tests\out\ /Fe tests\out\multitimer_tests.exe ^
   tests\test_*.cpp ^
   native\MultiTimer\src\timer\TimeMath.cpp ^
   native\MultiTimer\src\timer\TimerEngine.cpp ^
   native\MultiTimer\src
et\mini_json.cpp ^
   native\MultiTimer\src
et\BilibiliParse.cpp ^
   native\MultiTimer\src\config\ConfigStore.cpp ^
   native\MultiTimer\src\config\WifiStore.cpp ^
   native\MultiTimer\src\pages\PageRenderer.cpp ^
   native\MultiTimer\src\live\LiveFormat.cpp ^
   native\MultiTimer\src\utils\Surface.cpp
tests\out\multitimer_tests.exe
```

> 手工命令必须在仓库根目录运行，否则 `tests/out/data` 的相对路径会与测试进程不一致。

## 5. 覆盖清单

### TimeMath（24 个用例）

- 闰年、每月天数（含 2000/2100 世纪年）；
- `daysFromCivil` / `civilFromDays` 已知锚点（1970-01-01、1969-12-31、2000-01-01、2038 边界附近）；
- 天数与日期双向 round-trip（多年、多月、月末、闰日）；
- 分钟时区合法性（-720、840、+330、+345、+765、越界）；
- 日期/日期时间合法性（2000/2099 边界、非法月/日）；
- 本地墙上时间 ↔ UTC epoch（正/负时区、半小时/45 分钟偏移、往返）；
- 星期计算（1970-01-01 为周四）；
- 严格解析 `YYYY-MM-DD`、`HH:MM:SS`、`YYYY-MM-DD HH:MM:SS`（非法输入拒绝）；
- 格式化 `YYYY-MM-DD`、`HH:MM:SS`、`YYYY-MM-DD HH:MM:SS`、`formatEpochLocalTime`；
- 倒计时格式：`99:59:59` 仍为 `HH:MM:SS`、恰好 100 小时进入 `DdHH:MM:SS`、
  `4d04:02:02` 天数不补零、`formatDurationHMS` 小时可超两位。

### TimerEngine（19 个用例）

- 空目标/全禁用 → `STAGE_CLOCK`；单个/多个未来目标 → `STAGE_COUNTDOWN`；
- 所有目标过期 → `STAGE_COUNTUP`（从最近过期目标累计，`elapsedSeconds`）；
  到点当秒仍 `STAGE_COUNTDOWN`/余 0，下一秒进入正计时；
- 恰好到达目标（剩余 0）、过期跳过、排序去重、`targetIndex` 语义；
- 非法日期/时间/时区、预警边界（`remain == warnSeconds`、`warnSeconds=0` 关闭）；
- 小号字边界（`<100h` 普通、`>=100h` 小号）、跨日/跨月/跨年、改时区重算、负时区；
- `setConfig` 替换旧配置不残留、时钟阶段字段清零。

### ConfigStore（54 个用例，v3）

- `parseColor`：大小写、边界、非法格式；
- **v3 完整配置**：全部字段、页面顺序、live 页 2 槽位、`defaultPageId`、往返；
- 缺省值：`version` 可缺省、`warnSeconds` 默认 10、`colors` 默认
  （含 `dim` 与 **`countup`=`#00BFFF`**）、`display.splitBottomWhenIdle`、`targets` 缺省为空；
- 版本：`version` 存在但不是 3 → 拒绝（含 v2 专门错误文案）；
- `tzOffsetMinutes` 必填、-720..840、边界/越界；`warnSeconds` 0..600；
- `colors.clock/countdown/countup/warning/dim`：`#RRGGBB`、缺省、非法拒绝整份；
- `targets`：最多 12 项、超限拒绝、非法单项丢弃、`enabled` 缺省/类型；
- **`pages`**：必须存在、1..12 项、数组/对象类型、未知 type 拒绝；
  `id` 非法/重复自动生成；`title` ≤16 字符（UTF-8 边界）、`shortTitle` ≤8 ASCII；
  `enabled`/`metric` 解析；**live 页必须有 1..2 个 `rooms`**；
- **`rooms`**：`platform`/`room`(≤40, `[A-Za-z0-9_-]`)/`label`(≤3, 大写)/`enabled`/`metric`；
  同页重复房间后者丢弃为空槽；
- **`live`**：`source` 四枚举、`url` 只接受 `http://`（≤160/无空格）、
  `refreshSec` 15..300、`timeoutMs` 1000..5000；
- `defaultPageId` 引用存在页面，非法回退第一页；
- `toJson`：字段顺序稳定、颜色大写、空 targets 格式、字符串转义、v3 全量 round-trip；
- 真实磁盘 `save()`→`load()`；文件缺失/损坏/`version>=4` → 默认配置且保留原文件；
- **v2→v3 迁移**：保留 tz/warn/colors/targets/ntpServer，补 3 页（`p0 clock`、
  `p1 clock_countdown`、`p2 countdown`）、`dim`/`countup`/`display`/`live=off`、
  `defaultPageId=p0`；迁移后保存幂等；v2 无 version/非法时区路径；
- **v1→v3 迁移**：`tzOffset`/`points` → 当天目标；使用迁移后时区确定日期；
  `now` 无效时保留原文件不迁移；最多 8 项、非法单项丢弃；无 points 只迁移时区/颜色；
  迁移结果可再次保存并 round-trip 为 v3；
- `ntpServer`：默认/往返、host/host:port/IP/端口边界、非法拒绝、v1 迁移为空。

### mini_json（10 个用例）

- 对象/数组/字符串/整数/负数/布尔/null；转义（含 `\u` 代理对/控制字符）；
- 数字：`0`、负数、超 int、`1e3`、小数；`asInt/asInt64/asBool/asString` 默认值与钳位；
- 嵌套访问、越界/缺失键；`dump(-1)`/`dump(2)` round-trip；key/value 转义；
- 23 组非法输入全部返回 false；60 层可解析、1000 层/未闭合返回 false 不崩溃。

### WifiStore（19 个用例）

`parseIni`（7 个）：`[main]`/`[wifi]`、引号值、BOM、CRLF、注释、未知段、键名别名/分隔符；
`parseJson`（6 个）：`{"ssid","password"}`、别名、嵌套、非法/缺 ssid、`valid()`；
`parseJsonNetworks`（6 个，v2.1）：v2 列表、旧单对象、数组根/去重、非法/超 8 项、
`enabled` 非布尔丢弃、恰好 8 个允许。

### PageRenderer（44 个用例，v3.3）

- 度量契约表（`measureText`/advance 与固件一致）；
- `clock`：几何/颜色/无效时间/时区；
- `clock_countdown`：8+8 几何（基线 7/14）、**同一 `now` 上下两半**、
  无目标 `NO COUNTDOWN`（dim）/旧 blank 被忽略、预警只变下半、无效时间下半留空、
  ≥100h 仍 scale 1；目标过期后下半**正计时**使用 `colors.countup`
  （`renderer_split_page_countup_after_target`、`renderer_split_page_idle_shows_no_countdown`、
  `renderer_split_page_no_target_shows_no_countdown`）；
- `countdown`：scale 2、≥100h 小号、目标过期后**正计时**、无有效目标显示
  `NO COUNTDOWN`、无效时间、预警色、改时区立即重算；
- `datetime`：时间 `HH:MM:SS` scale 2（baseline 10）、日期 `YYYY-MM-DD` scale 1
  （baseline 15，dim）、时间无效 `--:--:--`（`renderer_datetime_page_shows_date_and_time`）；
- `live`：**上下两行几何与平台色**、label 自动/显式一致、数值压缩、失败/等待/全失败
  `--` dim、**0 是有效值**、无配置房间 `NO LIVE`、单房间下行留黑、陈旧 dim、
  禁用留黑、`status` 指标、槽位指标覆盖、`none` 指标、fit 阶梯；
- 横幅：版式/长标题截断/空标题不崩溃；覆盖层：两行/单行/截断；
- 越界 pageIndex / 空 pages 返回 false 并清屏。

### LiveFormat（24 个用例，v3）

- `formatCount`：`0..9999`、万/亿（1 位小数、进位升级单位）、≥10^10 截断、负数/无效 `--`；
- 指标格式化：数值、`status` → `LIVE`/`OFF`、`none`/`title` → `--`；
- 平台枚举/字母/颜色（契约色值）、label 默认生成/规范化/显式优先；
- `effectiveMetric`/`selectMetricValue`、指标/数据源/页面类型枚举 round-trip、
  `isCellConfigured`。

### BilibiliClient / BilibiliParse（17 个用例，v3）

- `room_id` 校验（1..20 位 ASCII 数字；空/空格/符号/全角/超长拒绝）；
- `getH5InfoByRoom` 解析：正常、**0 值有效**、`code != 0` 错误、缺字段；
- `get_info` 兜底解析：正常、错误；
- HTTP 响应解析：`Connection: close` 读到 EOF、chunked（含扩展/多段/trailer）、
  chunked 空 body、chunked 优先于 Content-Length、状态行/Content-Length、裸 LF 头、
  非法响应、截断 chunked、body 上限。

## 6. 本次通过的源码快照

| 文件 | 修改时间 | SHA256 前 16 位 |
| --- | --- | --- |
| `src/timer/TimeMath.h` | 2026-10-22 16:43:41 | `AF20F70D56826E64` |
| `src/timer/TimeMath.cpp` | 2026-10-22 16:51:00 | `4234A3868C93233B` |
| `src/timer/TimerTypes.h`（v3.3） | 2026-10-22 20:54:38 | `7FEBF0D7A51A1318` |
| `src/timer/TimerEngine.h`（v3.3） | 2026-10-22 20:54:27 | `11B79FCA60FE3AB0` |
| `src/timer/TimerEngine.cpp`（v3.3） | 2026-10-22 20:54:31 | `DCB7AAE94CA41174` |
| `src/config/ConfigStore.h`（v3） | 2026-10-22 19:26:48 | `60F2906E303DC135` |
| `src/config/ConfigStore.cpp`（v3.3） | 2026-10-22 20:54:57 | `448FE4E5E358880C` |
| `src/config/WifiStore.h` | 2026-10-22 18:33:41 | `C7954BDFB84EEBB9` |
| `src/config/WifiStore.cpp` | 2026-10-22 18:33:51 | `CFB4B05355AAB04E` |
| `src/net/mini_json.h/.cpp` | 2026-10-07 22:55:13 | `7B757393394C2B94` / `D8D6AAC46C73E158` |
| `src/net/BilibiliParse.h`（v3） | 2026-10-22 19:27:57 | `1E66051850772E27` |
| `src/net/BilibiliParse.cpp`（v3） | 2026-10-22 19:35:03 | `8A9B291A3CA06DED` |
| `src/pages/PageTypes.h`（v3.3，新增 datetime） | 2026-10-22 21:46:57 | `0EC19E6B169D0447` |
| `src/pages/PageRenderer.h`（v3.3） | 2026-10-22 20:40:57 | `41F38197915171AF` |
| `src/pages/PageRenderer.cpp`（v3.3，新增 datetime/NO COUNTDOWN） | 2026-10-22 21:47:06 | `D547A7E04FC4C0A4` |
| `src/live/LiveFormat.h`（v3） | 2026-10-22 19:25:53 | `7932C640078C02E5` |
| `src/live/LiveFormat.cpp`（v3） | 2026-10-22 19:26:02 | `C5457ED23FEC1A78` |
| `src/utils/Surface.h` | 2026-10-02 14:36:01 | `49FB8983F2792198` |
| `src/utils/Surface.cpp` | 2026-10-22 15:57:17 | `DA952148AF19FCE3` |
| `src/Fonts/TomThumb.h` | 2026-10-02 14:34:42 | `E32A444B3A83B00D` |

> Test sources（`tests/test_*.cpp`）随 v3/v3.3 更新：v3.0 新增
> `test_page_renderer.cpp`、`test_live_format.cpp`、`test_bilibili_client.cpp`，
> `test_config_store.cpp` 重写为 v3 覆盖（含迁移）；v3.2 在
> `test_page_renderer.cpp` 增加分屏正计时用例、`test_timer_engine.cpp` 扩展正计时断言；
> v3.3 在 `test_page_renderer.cpp` 增加 NO COUNTDOWN 与 datetime 用例
> （`test_page_renderer.cpp` 2026-10-22 21:48:40 / `C8302F1F450DAE24`）。
> `run_tests.ps1` 编译 `BilibiliParse.cpp` / `PageRenderer.cpp` / `LiveFormat.cpp` /
> `Surface.cpp`。`tests/out/v21_run.log` / `last_run.log` 仅作历史。

## 7. 测试边界与已知缺口

测试全部通过，覆盖的是“纯逻辑 + 配置持久化 + 页面渲染”层。以下模块**没有主机单测**，
依赖真机/网页自检/集成审查验证：

1. **TimeKeeper**：依赖 `StoragePreferences`、`settimeofday`、`time()`，属于设备平台层；
   恢复/持久化逻辑通过真机冷启动日志验证。
2. **PageHost / TimerPage**：依赖 FlyThings `Surface`/`PageBase`/SPI 与
   `StoragePreferences`（uiState 节流落盘），需真机目视与日志验证；
   其渲染部分已由 `PageRenderer` 纯函数用例覆盖。
3. **LiveService / LiveCache**：线程、调度、网络与缓存状态机；`LiveCache` 的状态阈值
   逻辑通过集成审查+真机验证（本套未直接编译 `LiveCache.cpp`）。
4. **HttpServer**：依赖 socket/线程与内嵌 gzip 页面；HTTP 路径通过真机 `/api/*`
   与 `check_web.js` 验证，未做 C++ 层单测。
5. **HttpClient / TlsClient**：socket 与 `dlopen` OpenSSL（v3.3 优先
   `/res/lib` 的 1.1.1w）；响应解析逻辑部分由 `BilibiliParse` 用例覆盖，
   真实网络/TLS 需真机验证（v3.3 已真机验证 B 站直连）。
6. **WifiBootstrap / SntpClient**：依赖平台 WiFi 与 UDP 123；
   每分钟校时、自定义服务器回退、AP 暂停等运行时行为没有主机单测。
7. **ConfigMode（SoftAP）**：依赖 `SoftApManager`/`NetManager` 与无线驱动；
   15 分钟超时、AP IP、80→8080 跳转需真机验证；v3.3 入口为**右键长按 3s**，
   物理按键的 LED 观感仍需用户确认。
8. 没有 CI；每次改源码后需要手工重跑本脚本与网页自检，并在真机复核关键路径。

> v3 的页面/直播/配置/migration 规则已由上述用例固定；若修改
> [`docs/V3-CONTRACT.md`](../docs/V3-CONTRACT.md) 的契约，必须同步改测试。

## 8. 维护提示

- 新增用例：在任意 `test_*.cpp` 中用 `TF_TEST(唯一名字) { ... }`，注册表自动收集，
  无需改 runner；文件名需匹配 `test_*.cpp`。
- 断言失败会打印 `tests/test_xxx.cpp:行号: 信息` 并中止当前用例，其余用例继续执行。
- 测试源码保持 **ASCII-only**（中文用例名会破坏 MSVC 本地代码页解析）；
  应用源码为 UTF-8，编译时用 `/utf-8`。
- `tests/out/` 是生成目录，可安全删除；再次运行会重建（包括 `tests/out/data`）。
- 若源码被并发修改，请以本次运行输出与第 6 节哈希确认结果对应的版本，必要时重跑。
- 主机配置目录宏只用于 PC 测试；**不要**把它加进设备工程或提交到
  `native/MultiTimer` 的构建配置。
- 修改 `tools/check_web.js` / `web-src/index.html` 后也要跑
  `node native/MultiTimer/tools/check_web.js`（当前 90/90）。



