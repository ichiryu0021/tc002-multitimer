# TC002 配置页可视化 QA 记录

日期：2026-10-22 16:03–16:13（Asia/Shanghai）

浏览器：`C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe`
版本：`155.0.4283.62`（CDP product token `Edg/155.0.4283.62`，headless）。

测试对象：

- 本地 mock：`http://127.0.0.1:18080/`，页面由 `native/MultiTimer/web-src/index.html` 原样提供。
- 真机：`http://192.168.31.124:8080/`，只读访问；没有点击保存、WiFi 重连或立即对时。
- 本批截图与 mock 测试对应的页面 SHA-256：
  `24E0428C0DCD03B148E15981C5B88B14D53D28990E97F39B5645CEB528A474FB`
  （当时本地文件和真机部署文件相同；之后本地文件又被外部更新到
  `88C1569BE9717C997FC38447B70F2B5BE1F87B02FDDFC554283AAF316C332CCA`，
  这一后续版本未参与本次截图。）
- 没有修改 `web-src/index.html` 或任何 C++ 文件。

## 重要工具限制：Edge headless 的 390px 窗口

Windows 上 Edge headless 会把小于约 500px 的 `--window-size` 布局视口压到
约 492px；`--screenshot` 仍输出 390px 宽的 PNG，实际是 492px 布局的裁切，
会制造“页面右侧被裁掉”的假象。原始 CLI 证据保留为
`mobile-cli-390.png` 和 `desktop-cli-1280.png`，不要把它们当作真实手机布局结论。

真实 390×1200 检查使用 CDP：

```powershell
msedge.exe --headless=new --disable-gpu --hide-scrollbars `
  --window-size=390,1200 --remote-debugging-port=9333 `
  --user-data-dir=tests/visual/.edge-profile-cdp about:blank

node tests/visual/cdp_probe.mjs --port 9333 --width 390 --height 1200 `
  --url http://127.0.0.1:18080/ --no-save `
  --viewport-screenshot tests/visual/mobile.png `
  --screenshot tests/visual/mobile-full.png `
  --out tests/visual/cdp-report-mobile.json
```

`cdp_probe.mjs` 只使用 Node 24 内置的 `fetch` / `WebSocket`，不需要 npm 包。

## 主要截图

| 文件 | 内容 |
| --- | --- |
| `mobile.png` | mock，真实 390×1200 CSS 视口，顶部对齐 |
| `desktop.png` | mock，真实 1280×1000 CSS 视口，顶部对齐 |
| `countdown-mobile.png` | mock 相对时间 fixture，绿色倒计时 12:50 |
| `mobile-autoscroll.png` | mock 加载后自然滚动到的位置（缺陷证据） |
| `real-mobile.png` | 真机，390×1200，已加载配置/设备信息/蓝色正计时 |
| `real-desktop.png` | 真机，1280×1000，同上 |
| `real-mobile-autoscroll.png` | 真机加载后自动滚动位置（缺陷证据） |
| `*-full.png` | CDP 全页截图（LED 区域在全页截图里可能是黑块，这是跨视口合成伪影，视口截图中的 LED 正常） |
| `mobile-cli-390.png` / `desktop-cli-1280.png` | 原始 Edge `--screenshot` 输出，仅保留作工具限制证据 |

报告：

- `cdp-report-mobile.json` / `cdp-report-desktop.json`：mock 布局、console、保存流程。
- `cdp-report-mock-relative.json`：绿色倒计时状态。
- `cdp-report-real-mobile.json` / `cdp-report-real-desktop.json`：真机布局、设备信息、console。
- `dom-mobile.html` / `dom-report.json`：mock 的 `--dump-dom` 快照与断言。
- `mock_requests_exact.jsonl` / `mock_requests_relative.jsonl`：mock 请求日志。
- `real-config-20261022-161144.json`：真机配置只读快照。
- `real-index.html`：真机部署页面副本，SHA-256 与上述 24E0… 相同。

## 结论摘要

- 真实 390px：无横向溢出（`scrollWidth == clientWidth == 390`）；所有按钮/输入在水平方向
  都在视口内；LED canvas 322×99.08，52:16 比例正确且完全在 `.led-wrap` 内；
  保存按钮 336×45、未被遮挡（位于页面下方，需正常滚动）。
- 1280px：同样无横向溢出，内容居中，LED 412×126.8。
- 配置加载成功，显示“已加载时钟配置”；真机显示
  IP `192.168.31.124`、SSID `JasonYiL`、设备时间（UTC+8）和 uptime。
- LED 确实绘制：真机/ mock 已过最后时间点时是蓝色正计时；
  相对时间 fixture 下是绿色倒计时数字。
- console：0 个 `console` error，0 个 pageerror / uncaught exception。
  只有两条浏览器提示：Canvas `getImageData` 建议 `willReadFrequently`，
  以及密码输入框不在 `<form>` 内（都是非阻断性提示）。
- 高优先级问题：页面首次加载时，`addStatus()` 里的
  `scrollIntoView({behavior:"smooth", block:"nearest"})` 会把用户从页面顶部
  自动滚到接近底部。mock 和真机均可复现：390px 视口从 `scrollY=0` 滚到
  `936`（max 964）；1280px 视口从 0 滚到 1126（max 1154）。
  用户初始看不到标题、连接卡、网络卡和 LED 模拟器上半部。
- 低优先级：时间点行的 ▲▼✕ 按钮实际为 40×38 CSS px，小于常见的 44×44 触控建议。
- 观察到一次（未复现）：16:10:25 的一次桌面页面加载把真机 IP/SSID 显示为“未知 /
  未连接”；随后直接 GET `/api/config` 和两次重新加载均正常。疑似真机瞬时状态或
  并发操作所致，不能据此单独断言前端 bug。
- 真机配置在测试期间被外部修改：从 `16:09/16:10` 变成 `16:10/16:11`（warnSeconds=5）；
  最终两张真机截图与后一组配置一致（均已过点，显示正计时）。本 QA 没有发出任何
  写请求；mock 里唯一一次保存是明确对 127.0.0.1 mock 做的功能验证。

## Mock 请求日志

`mock_requests_exact.jsonl`（exact fixture）：

- 6 × `GET /`
- 7 × `GET /api/config`
- 1 × `POST /api/config`
- 1 × `POST /api/time`

保存按钮验证触发的 POST 请求体：

```json
{"tzOffset":8,"warnSeconds":10,"colors":{"countdown":"#00FF00","warning":"#FF2020","countup":"#0080FF"},"points":[{"time":"10:00","enabled":true},{"time":"12:30","enabled":true}]}
{"epoch":1792656606}
```

`mock_requests_relative.jsonl`（倒计时 fixture）：

- 1 × `GET /`
- 1 × `GET /api/config`

mock 服务在检查结束后已停止（18080/9333 端口均已释放）。

## v2 真机只读复测（16:19）

部署页 SHA-256：`59C5B5F4F1CC61A8AD4E865FF4D75B5D10738B4DCF155CD9A41E0F683AFFCC7B`
（与当时本地 `web-src/index.html` 一致）。测试时真机配置为
points `16:17/16:18`、`warnSeconds=10`，已过最后时间点。

- 截图：`real-mobile-v2.png`（390×1200）、`real-mobile-v2-initial.png`
  （加载后自然状态，未强制滚顶）、`real-mobile-v2-full.png`。
- 初始 `scrollY`：0（加载轮询 61ms / 226ms、稳定 1127ms 三个采样点均为 0；
  maxScrollY=964）。页面高度 2164。**自动滚动已修复。**
- `computeFrame()` 返回：`text="00:01:42"`、`kind="COUNTUP"`、
  `hex="#0080FF"`、`blank=false`；无 `+` 前缀，格式为 `HH:MM:SS`。
- 红色预警本次未被触发（当前已过最后时间点），因此没有对“常亮红色、
  不再黑闪”做运行态截图验证；未修改配置。
- `clientWidth = scrollWidth = 390`，无横向溢出；所有控件水平方向在视口内。
- 时间点行 ▲▼✕ 实测均为 44×44 CSS px。
- 0 个 console error、0 个 pageerror；浏览器日志仅两条非阻断提示：
  密码框不在 `<form>` 中、Canvas `getImageData` 建议 `willReadFrequently`。
- 全程只读：没有点击保存、WiFi 重连或立即对时，状态条只有“已加载时钟配置”。

报告：`cdp-report-real-v2.json`、`cdp-real-v2-summary.txt`。
真机页面副本：`real-index-v2.html`；配置快照：`real-config-v2-20261022-161917.json`。
