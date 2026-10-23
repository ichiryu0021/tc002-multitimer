# AGENTS.md — AI / 二次开发入口

> 给 AI 助手和新维护者的第一份文件。读完本页即可定位代码、构建、测试和改动边界；
> 细节以 `docs/V3-CONTRACT.md` 与源码为准，README 里的“版本状态”可能滞后。

## 1. 项目是什么

Ulanzi TC002（52×16 RGB 像素时钟，Z21 / Linux ARM）的多功能计时器项目，由四个可独立运行的部分组成：

| 组件 | 技术栈 | 入口文件 | 运行位置 |
|---|---|---|---|
| 固件应用 | C++ / FlyThings SDK | `native/MultiTimer/src/logic/mainLogic.cc` | 设备 `/res/lib/libzkgui.so`（替换官方 App） |
| 手机配置页 | 单文件 HTML/JS（零外链） | `native/MultiTimer/web-src/index.html` | 设备 `:8080`，gzip 内嵌进固件 |
| 直播中继 bridge | Python 3 + requests + Playwright | `tools/live-bridge/bridge.py` | PC / NAS，默认 `:8081` |
| TC002 桌面助手 | Python + pywebview(WebView2) + PyInstaller | `tools/live-bridge/forwarder_gui.py` | Windows 单文件 exe |

设备固件启动后监听 `0.0.0.0:8080`（另试 `:80` 并 302 跳转），桌面助手和手机页都通过这套 HTTP API 读写配置。

## 2. 不可违反的硬约束

- **资源**：整机 RAM 约 35 MB、`/res` 8 MiB、屏幕 52×16。禁止引入大依赖、大响应、大缓冲区。
- **网络能力**：设备没有 libcurl、没有 OpenSSL 头文件；HTTPS 只能用 `res-extra/lib` 随镜像安装的 OpenSSL 1.1.1w（B 站直连）。
- **HTTP**：8080 无鉴权、无 TLS、无流控，只应部署在可信局域网；请求头 2 KB、请求体 16 KB、收发超时 3 s、最多 8 并发。
- **bridge `/live`**：最多 8 个房间，响应体硬上限 **4096 B**；数值 `0` 是有效值，不能用 0 判断失败。
- **文案**：`shortTitle` ≤8 个**可打印 ASCII**，房间 `label` ≤3 个 ASCII，`title` ≤16 字符（不上屏）。
- **配置**：`/data/multitimer.conf` 必须是 `version: 3`，`POST /api/config` 只接受 v3。
- **渲染**：每帧只取一次 `time(NULL)`，所有视图共用同一个 `now`。
- **生成物**：`native/MultiTimer/src/web/editor_gz.h` 由脚本生成，**禁止手改**（见 §4.3）。

## 3. 目录地图

```text
native/MultiTimer/           固件工程（FlyThings / Eclipse CDT）
  src/logic/mainLogic.cc     应用启动、旋钮/按键、页面宿主装配
  src/pages/PageRenderer.*   五类页面像素渲染（52×16），改显示先看这里
  src/pages/PageHost.*       页面列表、当前页、切页横幅、持久化
  src/live/LiveService.*     B 站直连 + bridge 轮询、缓存、失败回退
  src/net/HttpServer.*       手机页 + 全部 /api/*（8080）
  src/config/ConfigStore.*   v2→v3 迁移、校验、原子保存
  src/web/editor_gz.h        手机页预压缩字节（构建产物）
  web-src/index.html         手机页源文件
  tools/build_web_asset.py   index.html → editor_gz.h
  tools/check_web.js         手机页逻辑自检（90 项）
  res-extra/lib/             随镜像安装的 OpenSSL 1.1.1w
  third_party/               SDK / OpenSSL 来源与许可

tools/
  build.ps1                  FlyThings headless 编译
  make-image.ps1             以官方 stock /res 为基线生成 dist/update.img
  make-minimal-image.ps1     生成可公开分发的 dist-minimal/update.img
  flash-update.ps1           ADB 刷机（默认 DryRun，-Confirm 才执行）
  live-bridge/
    bridge.py                直播中继（也可单独命令行运行）
    device_client.py         设备 HTTP 封装（status/config/wifi/live/time/wifi 网络）
    desktop_api.py           pywebview 暴露给前端的全部方法
    desktop_ui.html          桌面助手全部 UI（单文件，无外链）
    forwarder_gui.py         桌面助手进程入口
    build_gui.ps1            PyInstaller 打包
    test_bridge.py           bridge 单测（40 项）

tests/                       主机单测（C++，211 项）+ 运行脚本
docs/                        契约、设计、部署、验证记录（地图见 §9）
dist/  dist-minimal/         构建/镜像产物（分发边界见 §7）
```

## 4. 构建与测试

### 4.1 固件

```powershell
powershell -ExecutionPolicy Bypass -File tools/build.ps1
# 默认：native/MultiTimer → build/ide-workspace，Release
# 产物：build/ide-workspace/MultiTimer/Release/libzkgui.so
```

- 依赖 FlyThings IDE，默认路径 `D:\zkswe\FlyThingsIDE\bin\ide.exe`（可用参数覆盖）。
- 脚本每次重建 workspace，以 `libzkgui.so` 是否生成判定成功；IDE 的 clean 报错不代表构建失败。

### 4.2 镜像与刷机

```powershell
powershell -File tools/make-image.ps1          # 需要本地官方 stock /res 基线 → dist/update.img
powershell -File tools/make-minimal-image.ps1  # 无 stock 素材 → dist-minimal/update.img
powershell -File tools/flash-update.ps1        # DryRun 预览
powershell -File tools/flash-update.ps1 -Confirm   # 真刷：adb push + 升级属性 + 重启
```

刷机会**清空 `/data`**，有变砖风险；不能写 `/res` 或 `/tmp` 大文件。详见 `docs/FLASH-RUNBOOK.md`。

### 4.3 改手机页后必须重新生成 + 自检

```powershell
python native\MultiTimer\tools\build_web_asset.py
node native\MultiTimer\tools\check_web.js
```

再编译固件。设备读的是 `editor_gz.h`，不是 `web-src/index.html`。

### 4.4 测试

```powershell
powershell -File tests\run_tests.ps1              # 211/211
cd tools\live-bridge; python -m unittest -v test_bridge   # 40 项
```

### 4.5 桌面助手

```powershell
cd tools\live-bridge
python -m pip install -r requirements-gui.txt
python forwarder_gui.py                            # 源码运行
powershell -File build_gui.ps1                     # 打包
# 产物：tools/live-bridge/dist/TC002Desk.exe
```

打包依赖 `webview`、`playwright`、PyInstaller；`desktop_ui.html` 通过 `--add-data` 嵌入，不要改成外部路径。

## 5. 设备 HTTP API（`:8080`）

| 方法 | 路径 | 用途 |
|---|---|---|
| GET | `/`、`/index.html` | 内嵌手机页（gzip） |
| GET/POST | `/api/config` | v3 配置读写；GET 返回 `{ok,config,device,ui,live}`，`ui`/`live` 只读 |
| POST | `/api/page` | `{"id":"p1"}` / `{"index":1}` 切页；`{"reset":true}` 清记忆 |
| GET | `/api/status` | `{ok,device,state}`，含当前页 `state.page` 与 `state.mode` |
| POST | `/api/time` | `{"epoch":...}` 写系统时钟 |
| GET | `/api/live/status` | 数据源健康 + 房间快照（≤24） |
| POST | `/api/live/refresh` | 触发一次异步刷新 |
| POST | `/api/live` | `push` 模式推送（body 同 bridge 响应） |
| GET | `/api/wifi` | 当前网络 + 网络列表（不返回密码） |
| POST | `/api/wifi/list` | 保存 WiFi 列表（≤8）并异步重连 |
| POST | `/api/wifi` | 兼容单网络接口（空密码不清空原密码） |

错误统一 `{"ok":false,"error":"..."}`。配网 SoftAP 只能右键长按 3 s 进入/退出，**没有 HTTP 接口**。

## 6. 配置 schema v3（精简）

```json
{
  "version": 3,
  "tzOffsetMinutes": 480,
  "warnSeconds": 10,
  "ntpServer": "",
  "colors": {"clock":"#FFFFFF","countdown":"#00FF00","countup":"#00BFFF","warning":"#FF2020","dim":"#808080"},
  "targets": [{"date":"2026-10-23","time":"19:25:00","enabled":true}],
  "live": {"source":"hybrid","url":"http://192.168.1.10:8081/live","refreshSec":30,"timeoutMs":3000},
  "defaultPageId": "p0",
  "pages": [{"id":"p0","type":"clock","title":"时钟","shortTitle":"CLOCK","enabled":true}]
}
```

| 字段 | 规则 |
|---|---|
| `tzOffsetMinutes` | -720..840，必填 |
| `warnSeconds` | 0..600 |
| `ntpServer` | 空或 `host` / `host:port`，≤64 |
| `targets` | ≤12，每项 `{date,time,enabled}`，非法项丢弃 |
| `live.source` | `off` / `bridge` / `hybrid` / `push`，默认 `off` |
| `live.url` | 仅 `http://`，≤160；`bridge`/`hybrid` 时用 |
| `live.refreshSec` | 15..300，默认 30 |
| `pages` | 1..12；`id` 匹配 `[a-z0-9_-]{1,8}` 且唯一 |
| `pages[].type` | `clock` / `clock_countdown` / `countdown` / `live` / `datetime` |
| `pages[].rooms` | 仅 live 页，1..2 项；房间号 ≤40，`label` ≤3 ASCII |
| 指标 | `online` / `likes` / `fans` / `status` / `title` / `none` |

完整规则、渲染坐标、迁移逻辑见 `docs/V3-CONTRACT.md`。

## 7. 桌面助手内部结构

- `forwarder_gui.py`：单窗口 pywebview，`file://` 加载 `desktop_ui.html`，`js_api=DesktopApi`；存储目录 `%LOCALAPPDATA%\TC002Desk\webview`。
- `desktop_api.py`：前端可调的 **全部** 方法（`app_info`、`start_forwarder`、`scan_devices`、`device_state`、`save_targets`、`save_pages`、`save_display`、`sync_network_time` 等）；方法名必须与 HTML 里的 `call("...")` 一致。
- `device_client.py`：设备 HTTP 封装，设备发现（扫描本机各 `/24` 的 8080 + SoftAP `192.168.100.1`）。
- **保存模式（重要）**：任何写操作都先 `GET /api/config`，只替换目标字段，再 `POST /api/config`。禁止发送部分配置，否则会清掉页面/目标/WiFi。
- 前端轮询：转发状态 1.5 s、日志 0.8 s、设备 5 s；`pagesSig`/`displaySig` 防止轮询重绘覆盖正在编辑的内容（`pagesDirty`/`displayDirty`）。
- **网络校时**：PC 用 NTP（默认 `ntp.ntsc.ac.cn`，依次回退）取 UTC，失败回退 HTTP `Date`，再 `POST /api/time`；不使用电脑本地时钟。
- 快手：bridge 启动无头 Edge（`channel="msedge"`），失败回退本机 Chromium；首次调用慢、可能 `rate_limited`。
- 发布边界：`dist/update.img` 是含官方 stock `/res` 的完整镜像；可公开分发的只有 `dist-minimal/update.img`（见 §9 与仓库 README）。

## 8. 常见改动配方

1. **改屏幕显示**：`src/pages/PageRenderer.cpp` + `tests/test_page_renderer.cpp`，跑主机单测；像素断言是唯一可靠回归。
2. **加/改 API**：`src/net/HttpServer.cpp`，同步更新 `docs/V3-CONTRACT.md` §6 与 `native/MultiTimer/README.md` 的 API 表。
3. **改手机页**：改 `web-src/index.html` → §4.3 生成 + 自检 → 编译；不要直接改 `editor_gz.h`。
4. **改直播取数**：`tools/live-bridge/bridge.py`，同步 `docs/LIVE-BRIDGE.md`；注意 4096 B 上限与房间号格式。
5. **改桌面 UI**：`desktop_ui.html` + `desktop_api.py`（两边方法名/参数一一对应）→ `build_gui.ps1` 重新打包。
6. **改配置字段**：`ConfigStore` 校验 + 迁移 + 手机页 + 桌面“页面与显示”四处都要改，缺一不可。

## 9. 文档地图

| 文档 | 解决什么问题 |
|---|---|
| `docs/V3-CONTRACT.md` | **最高优先级**：v3 配置 schema、页面渲染、bridge 协议、API 变更、验收 |
| `docs/LIVE-BRIDGE.md` | bridge 部署、平台边界、防火墙、自测记录、桌面助手页签 |
| `docs/FLASH-RUNBOOK.md` | 刷机/回滚、`/res` 固化、ADB 流程 |
| `docs/HANDOFF-native.md` | 原生工程交接、文件职责、已知问题 |
| `docs/DESIGN-pages-v3.md` / `DESIGN-web-v3.md` | 页面系统与手机页设计（含像素坐标、自检项） |
| `docs/RESEARCH-live-apis.md` | 三平台接口调研、限流与签名结论 |
| `docs/QA-v3-final.md` | v3.3 最终验收记录与遗留项 |
| `tests/README.md` | 单测清单与运行方式 |
| `native/MultiTimer/README.md` | 工程构建、目录、API 细节、生成资源流程 |

## 10. 已知坑

- `/api/config` GET 里的 `ui`、`live` 是只读摘要，POST 不接受它们。
- 0 是有效指标值；缺失指标应 `--`，不要用 0 判失败。
- v2 配置会一次性迁移到 v3 并立即保存；不要写回 v2。
- 设备自身 NTP 设置与桌面助手校时无关；部分运营商拦截 UDP 123，需保持 HTTP `Date` 兜底。
- 桌面助手：旧 exe 未退出会锁 WebView2 目录（`0x8007139F`）并占用 8081；重打包前先关闭。
- 快手依赖 Playwright；bridge 打包版需 `--collect-all playwright` 才能工作。
- 手机页“立即对时”用浏览器时间，浏览器不准设备就跟着不准。
- `dist/update.img` 含官方 stock 素材，**不要**擅自提交到公开仓库或 Release；对外分发用 `dist-minimal/`。

## 11. 提交前检查清单

- [ ] `tests\run_tests.ps1` 全绿（改 C++ 时）
- [ ] `node native\MultiTimer\tools\check_web.js` 全绿 + `editor_gz.h` 已重新生成（改网页时）
- [ ] `python -m unittest -v test_bridge` 全绿（改 bridge 时）
- [ ] 桌面助手改动已重打包，并实机点过对应页签（改 UI/API 时）
- [ ] 契约文档与代码同步（新增/修改 API、配置字段、页面行为）
- [ ] 没有把 `dist/update.img`、本地 profile、临时测试脚本带进提交
