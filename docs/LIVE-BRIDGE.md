# TC002 MultiTimer v3 直播数据中继（bridge-v3）

> 状态：**可用**（2026-10-22 19:2x 本机实测）。实现范围：`tools/live-bridge/**` 与本文件；
> 未改固件/网页/设备，未 `git commit`，未使用任何账号或真实 cookie/token。
>
> 协议以 [`V3-CONTRACT.md`](V3-CONTRACT.md) §5 为准；平台结论来自
> [`RESEARCH-live-apis.md`](RESEARCH-live-apis.md)（2026-10-22 实测）。

## 0. 一句话与部署建议

设备（TC002）只发一条**局域网明文 HTTP GET**，拿到一份 ≤4 KB 的统一 JSON；
所有平台差异、cookie、风控、JS 签名都由这台 PC/NAS 上的 `bridge.py` 承担。

**2026-10-22 用户拍板后的分工：**

| 数据源 | 由谁取 | 说明 |
|---|---|---|
| B 站 | **设备直连 HTTPS**（v3 的 P4 模式） | 设备只取 B 站时最省事；bridge 里的 B 站实现**保留**，作为兜底/混排 |
| 抖音 | **bridge** | 匿名 `ttwid` + 网页/手机端点，受 IP 风控 |
| 快手 | **bridge + Playwright** | 无浏览器时明确返回 `kuaishou_requires_browser` |

- 设备端每页 **2 个房间**（本次拍板）；bridge 协议本身支持一次 **1–8 个**房间，
  所以一页 4 个、或 B 站直连 + 抖音/快手混排都能用。
- bridge 的 B 站返回与抖音/快手**同一套字段**，因此设备把某一格切回 bridge 时
  不需要改解析。

## 1. 设备 ↔ bridge 契约

### 1.1 请求

```http
GET /live?rooms=bilibili:6,douyin:611063790284 HTTP/1.1
Host: 192.168.31.66:8081
Accept: application/json
Connection: close
```

- `rooms` 逗号分隔，`platform:room`，平台名 `bilibili` / `douyin` / `kuaishou`（不区分大小写）；
- 房间号字符集 `[A-Za-z0-9_-]`，≤40 字符；重复项自动去重，顺序保持请求顺序；
- 最多 8 个；超过返回 `400 {"ok":false,"error":"too_many_rooms"}`；
- 请求行总长上限 1 KB（设备实际约 60–120 B，余量充足）。

房间号形式：

| 平台 | 形式 | 例子 |
|---|---|---|
| B 站 | 数字短号或真号 | `6`、`21452505`、`7734200` |
| 抖音 | 12 位左右 `web_rid`（分享链接里的数字） | `611063790284` |
| 抖音 | 19 位数字 `room_id`（走手机端点，无需 cookie） | `7076284313080843019` |
| 快手 | `principalId` 字符串（`live.kuaishou.com/u/<id>`） | `3xwnf96yetb5ewm` |

### 1.2 响应

```json
{"ok":true,"ts":1792668348,"rooms":[
  {"key":"bilibili:6","platform":"bilibili","room":"6","ok":true,
   "online":198822,"likes":4044024,"fans":16896338,"status":1,
   "title":"【预告】24号1点全球总决赛瑞士轮"},
  {"key":"douyin:611063790284","platform":"douyin","room":"611063790284","ok":true,
   "online":25,"likes":23561,"status":1,"title":"温柔和可爱都给你"}]}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `ok` | bool | 顶层：请求被正常处理即 `true`；**房间级失败看 `rooms[].ok`** |
| `ts` | int | bridge 本地 Unix 秒 |
| `rooms` | array | 与请求顺序一致（去重后） |
| `key` | string | `platform:room`，可用来对齐配置 |
| `platform` / `room` | string | 规范化后的平台名 / 原样房间号 |
| `ok` | bool | 该房间本次是否取到数据 |
| `online` | int | 在线人数 / 人气值（B 站是**人气**，非并发人数） |
| `likes` | int | 本场累计点赞 |
| `fans` | int | 粉丝数（平台不公开时**字段缺失**） |
| `status` | int | **1 = 正在直播，0 = 未在播**（未开播/已结束/轮播/取不到状态） |
| `title` | string | 标题，默认截断 48 字符；v3.0 屏幕不显示，保留给网页/推送 |
| `error` | string | 仅失败房间出现，见 §8 错误码 |

规则（与契约一致）：

1. **数值 `0` 是有效值**（未开播房间的 online/likes 就是 0）。设备只在
   `ok:false`、房间缺失、或字段**不存在**时显示 `--`，绝不能用 0 判断失败。
2. 平台不提供的指标**直接省略**，不用 0 顶替：例如 B 站 `get_info` 兜底路径没有
   `likes`，抖音 `web_rid` 网页路径没有 `fans`。
3. `status` 已归一化为 1/0（B 站原始 `live_status` 2=轮播按 0 处理；抖音原始
   `status` 2=直播→1、4=已结束→0）。设备无论直连 B 站还是走 bridge，显示语义一致。
4. 响应体**硬上限 4096 B**：先按 `--title-max`（默认 48 字符）截断标题；仍超限就把
   标题压到 24 字符；再超限就整体去掉 `title` 字段。实测 8 个房间 1344 B、
   4 个房间 690 B、2 个房间 370 B。
5. HTTP 语义：正常请求一律 `200`（即使所有房间都失败）；`400` 请求本身非法；
   `404` 未知路径；`405` 非 GET；`500` 内部异常。
   `Content-Type: application/json; charset=utf-8`，带 `Content-Length` 与
   `Connection: close`（设备不需要 chunked）。
6. 顶层 `ok:false` 只在请求级错误（参数缺失/过多/格式错）时出现。

### 1.3 缓存 / 超时 / 限频（bridge 侧）

| 项 | 默认 | 说明 |
|---|---|---|
| 缓存 TTL | 20 s | 有效 TTL = `max(--cache-ttl, 平台最小间隔)`；B 站 10 s、抖音/快手 60 s |
| 上游超时 | 4 s | connect ≤3 s；只影响后台线程，不阻塞 `/health` |
| 响应预算 | 2.5 s | 设备 `timeoutMs` 默认 3000，bridge 在 2.5 s 内必答 |
| 超时兜底 | ≤120 s 旧值 | 刷新仍在途时，返回最近一次成功值（设备不会看到闪烁的 `--`） |
| 单房间并发 | 1 | 同一房间的并发请求合并成一个上游请求（single-flight） |
| 失败重试 | **不立即重试** | 按设计文档 §5.4：下一个轮询周期再试，避免风控/雪崩；B 站另有端点级兜底 |
| 抖音限流熔断 | 180 s | 出现 `200`+0 字节/403/429 后，整个抖音平台暂停 180 s，直接返回 `rate_limited` |
| B 站端点降级 | 600 s | `getH5InfoByRoom` 连续 2 次非房间级失败后，10 分钟内只走 `get_info` |
| 缓存条目 | ≤64 | 有界内存，LRU 式淘汰最旧 |

线程模型：`ThreadingHTTPServer`（每连接 1 线程）+ 固定 `ThreadPoolExecutor`
（默认 8 个工作线程）取数；HTTP 响应线程只等 `deadline`，慢请求留在后台继续并把结果
写进缓存，**下一次轮询直接命中**。常驻内存约十几 MB（Python 基线为主），
单响应体按 256 KB–2 MB 上限流式读取，不做无界读取。

## 2. 快速开始（Windows）

### 2.1 安装与启动

```powershell
cd D:\Project\ulanzi\tools\live-bridge

python --version                       # 需要 3.9+（本机实测 3.14.7）
python -m pip install -r requirements.txt

python bridge.py                       # 0.0.0.0:8081
# 指定网卡/端口：
python bridge.py --host 0.0.0.0 --port 8081
```

启动日志会直接打印可用地址（本机实测）：

```
19:25:40 INFO    MultiTimer live bridge 3.0.0 listening on 0.0.0.0:8081
19:25:40 INFO    cache ttl 20s (per-platform minimum: {'bilibili': 10.0, 'douyin': 60.0, 'kuaishou': 60.0})
19:25:40 INFO    response deadline 2.5s, upstream timeout 4.0s
19:25:40 INFO    device url: http://192.168.31.66:8081/live?rooms=bilibili:6
19:25:40 INFO    kuaishou: Playwright not installed -> kuaishou rooms return kuaishou_requires_browser
```

### 2.2 设备端配置

网页 `/api/config` 里：

```json
"live": {"source":"bridge","url":"http://192.168.31.66:8081/live","refreshSec":30,"timeoutMs":3000}
```

- `url` 必须是 `http://`（v3.0 设备端不支持 https），端口与 `--port` 一致；
- 设备每 `refreshSec`（15–300，默认 30）轮询一次，一次只带当前页的房间；
- 切页/旋钮按键会立即刷新（bridge 侧无状态，直接再请求一次即可）；
- bridge 默认 2.5 s 必答（`--deadline-ms`）。**若把设备 `timeoutMs` 设成小于 2500**，
  请同步把 `--deadline-ms` 降到 `timeoutMs - 300`，否则设备会先超时丢包。

### 2.3 本机自测

```powershell
# 契约 JSON（单房间）
curl.exe "http://127.0.0.1:8081/live?rooms=bilibili:6"

# 健康检查（不含任何凭据）
curl.exe "http://127.0.0.1:8081/health"

# 一次性取数，打印 JSON + 人读摘要（0=全成功，2=有房间失败，1=内部错误）
python bridge.py --once --rooms bilibili:6
python bridge.py --once --rooms bilibili:3,bilibili:21452505 --pretty
python bridge.py --once --rooms kuaishou:3xwnf96yetb5ewm
```

### 2.4 端口占用排查

```powershell
netstat -ano | findstr :8081
Get-NetTCPConnection -LocalPort 8081 -State Listen
```

若被占用，换 `--port 8082` 并同步改设备 `live.url`。

## 3. 平台支持与边界

### 3.1 B 站（可选/兜底 —— 设备优先直连）

| 项 | 实现 |
|---|---|
| 主端点 | `GET https://api.live.bilibili.com/xlive/web-room/v1/index/getH5InfoByRoom?room_id=<id>` |
| 兜底 | `GET https://api.live.bilibili.com/room/v1/Room/get_info?room_id=<id>` |
| 凭据 | **无 cookie / 无登录 / 无签名 / 无 WBI** |
| 取到的指标 | `title`、`live_status`、`online`（人气）、`like_info_v3.total_likes`、`anchor_info.relation_info.attention` |
| 失败判定 | HTTP 恒 200，**必须看 `code`**；`code != 0` 视为失败 |
| 错误映射 | `code 1/19002000/19002001/-400` → `not_found`；`-352/-412` → `rate_limited`；其它 → `bili_code_<n>` |
| 降级 | h5 非房间级失败连续 2 次 → 10 分钟只用 `get_info`（该路径没有 `likes`） |
| 房间号 | 纯数字 1–12 位，短号/真号都行 |

B 站是唯一"设备直连也稳"的平台（约 4 KB、无 cookie、实测 20 req/10 s 无风控），
所以当前部署优先设备直连；bridge 这份实现用于：只有 bridge 一条链路的场景、
设备直连失败时的兜底、以及 B 站+抖音/快手同一页混排。

已知风险：`getH5InfoByRoom` 是网页内部接口，B 站已把 `getInfoByRoom` 收紧成 `-352`，
未来可能轮到 h5；届时 bridge 会自动退到 `get_info`（丢 `likes`）。

### 3.2 抖音（bridge 必需）

两条路径，按房间号形式自动选择：

| 输入 | 路径 | cookie | 可用指标 |
|---|---|---|---|
| 12 位左右数字（`web_rid`） | `GET https://live.douyin.com/webcast/room/web/enter/?...&web_rid=<id>` | 匿名 `ttwid`（`HEAD https://live.douyin.com/` 的 `Set-Cookie`，只存内存，2 h 过期） | `online`（`user_count_str`，支持 "1.2万"）、`likes`、`status`、`title`；**无 `fans`** |
| 19 位数字（`room_id`） | `GET https://webcast.amemv.com/webcast/room/reflow/info/?...&room_id=<id>` | **完全不需要 cookie** | `online`、`likes`、`fans`（`owner.follow_info.follower_count`）、`status`、`title` |

失败语义（实测结论）：

- **`HTTP 200` + 0 字节是抖音的统一拒绝信号**（无 cookie / 被限流 / 房间不存在都这样），
  一律当失败：`ok:false, error:"rate_limited"`；
- `403/429` 同样按 `rate_limited`；
- 一旦判为限流，**整个抖音平台暂停 180 s**（`--rate-block`），期间不再发任何抖音请求，
  只返回 `rate_limited`；**不做立即重试**，避免把 IP 配额烧穿；
- 房间已结束是正常数据：`status:0`、`online:0`，`likes` 仍有历史累计值；
- 网页 `enter` 单个响应 400–480 KB（bridge 侧按 2 MB 上限流式读取，设备完全看不到）。

现实边界：匿名 `web_rid` 路径**能用但不稳**。调研实测十几次请求后进入 `200`+0 字节，
换全新 `ttwid` 也不恢复（IP 级）。因此：

- 4 个抖音房间 × 30 s 轮询 = 8 次/分钟，**会踩线**；建议抖音房间 ≤2 个、`refreshSec` ≥30，
  或直接使用 19 位 `room_id` 走 `reflow`（另一套风控，宽松得多，且不需要 `ttwid`）；
- 被限流后等 3–30 分钟再试，bridge 不会自己猛冲。

### 3.3 快手（bridge + Playwright，实验性）

无签名直连**不可行**（`livedetail` 恒 `result:2`，SSR 也是"请求过快"；签名
`__NS_hxfalcon` 是逐请求现算的 JS）。所以 bridge 只做一条路：

| 未安装 Playwright | 直接 `ok:false, error:"kuaishou_requires_browser"`（**不发任何网络请求**） |
|---|---|
| 已安装 | 无头 Chromium 打开 `https://live.kuaishou.com/u/<principalId>`，等 `window.__INITIAL_STATE__`，取 `watchingCount` / `likeCount`；拿不到时再退回 DOM 文本（"1376人在看"） |

安装（可选）：

```powershell
python -m pip install playwright
python -m playwright install chromium
```

行为与限制：

- 浏览器只在**第一次**遇到快手房间时懒启动（独立线程持有，sync API 不能跨线程用）；
- 首次打开页面约 3–10 s（估算，本机未装 Chromium，未实测），超过设备 2.5 s 预算 →
  第一次轮询该格是 `timeout`，后台完成后**下一次轮询**就有数据；
- 页面显示"请求过快，请稍后重试"（`errorType.type == 2`）→ `rate_limited`；
- 只能拿 `online`（`watchingCount`）与 `likes`（`likeCount`）；**没有 `fans`**，
  `status` 由 `isLiving` 归一化；标题只在页面能给出时才有；
- 未开播房间返回 `status:0` + 无数值，不会当成失败；
- 需要约 300–500 MB 磁盘与 200–400 MB 内存；OpenWrt 小路由器跑不动
  （见调研文档 §6.8），请放在 PC/NAS/树莓派 4+。

### 3.4 指标矩阵

| 指标 | B 站 h5 | B 站兜底 | 抖音 `web_rid` | 抖音 19 位 | 快手（浏览器） |
|---|---|---|---|---|---|
| `online` | ✅ 人气 | ✅ | ✅ | ✅ | ✅ 在播时 |
| `likes` | ✅ | ❌ | ✅ | ✅ | ✅ |
| `fans` | ✅ | ✅ | ❌ | ✅ | ❌ |
| `status` | ✅ | ✅ | ✅ | ✅ | ✅ |
| `title` | ✅ | ✅ | ✅ | ✅ | ⚠️ 页面能给才有 |

缺失的指标字段直接不出现，设备显示 `--`；**不要**用 0 代替。

## 4. 命令行参数

| 参数 | 默认 | 说明 |
|---|---|---|
| `--host` | `0.0.0.0` | 监听地址；只给本机用可设 `127.0.0.1`（但设备就连不上了） |
| `--port` | `8081` | 端口 |
| `--cache-ttl` | `20` | 缓存秒数（建议 10–30）；实际有效值还会被平台最小间隔抬高 |
| `--timeout` | `4.0` | 单个上游请求超时（秒，1–30） |
| `--deadline-ms` | `2500` | 给设备的响应预算；超时先用旧值/`timeout` 占位，后台继续取 |
| `--stale-max` | `120` | 刷新在途时最多返回多旧的旧值 |
| `--rate-block` | `180` | 抖音被拒后的全局暂停秒数 |
| `--workers` | `8` | 取数线程数（1–32） |
| `--title-max` | `48` | 标题截断字符数；`0` = 完全不返回标题（最省字节） |
| `--log-level` | `INFO` | `DEBUG` 会打印每个房间的取数耗时与指标 |
| `--once` | 关 | 取一次就退出（不启动服务器） |
| `--rooms` | `bilibili:6` | `--once` 用的房间列表 |
| `--pretty` | 关 | `--once` 输出缩进 JSON（仅人读，不保证 4 KB） |
| `--version` | | 打印版本 |

`--once` 退出码：`0` 全部成功、`2` 有房间失败（JSON 仍会打印）、`1` 绑定/内部错误。

## 5. 端口 / 防火墙 / 网络

### 5.1 Windows 防火墙（推荐只放私有网络）

```powershell
New-NetFirewallRule -DisplayName "MultiTimer live bridge (TCP 8081)" `
  -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8081 -Profile Private

# 删除
Remove-NetFirewallRule -DisplayName "MultiTimer live bridge (TCP 8081)"
```

`netsh` 等价写法：

```
netsh advfirewall firewall add rule name="MultiTimer live bridge" dir=in action=allow protocol=TCP localport=8081 profile=private
```

### 5.2 Linux（NAS / 树莓派）

```bash
sudo ufw allow from 192.168.31.0/24 to any port 8081 proto tcp
# 或 firewalld:
sudo firewall-cmd --permanent --add-port=8081/tcp && sudo firewall-cmd --reload
```

### 5.3 网络注意事项

- 设备和 PC 必须在**同一网段/同一 VLAN**；很多路由器开了 **AP 隔离/客户端隔离**，
  会导致设备连不上 PC，需要关掉或给 IoT 单独放行；
- 不要做端口映射/公网暴露（协议**没有鉴权**，设计上只信任局域网）；
- 多网卡机器（VMware/Hyper-V/WSL）启动日志会列多个地址，选和 TC002 同网段的那个；
- Windows 睡眠会断服务；常驻请用下面 §7 的方式并关掉睡眠。

## 6. 安全与隐私（无账号）

- **不需要账号、不登录、不提交表单、不发送弹幕/点赞/关注**；只做公开页面/接口的 GET；
- 唯一涉及的 cookie 是**匿名** `ttwid`：仅存在内存，2 小时过期，**不写磁盘、不进日志**；
- 不落任何用户 cookie/token；配置里也没有这类字段；
- bridge **只读**：`POST /live` 返回 405，整个服务没有任何写操作；
- 响应里不含 cookie、签名、主播个人信息（只有房间标题/公开计数）；
- 访问日志只有：方法、路径（**不含 query**）、状态码、耗时、客户端 IP、房间号与错误码；
- 平台用户协议限制未授权抓取：请定位为**个人、本地、非商业**用途，
  不绕过登录/风控、不二次分发、不对外提供数据；
- 若你自行给浏览器加登录态（本实现**不支持**），账号风险自负；给设备塞 cookie 没有意义
  （抖音缺的是签名、快手签名逐请求现算）。

## 7. 常驻运行（可选）

### 7.1 Windows：计划任务 / NSSM

最简单的是"登录时启动"计划任务：

```powershell
$py = (Get-Command python).Source
$script = "D:\Project\ulanzi\tools\live-bridge\bridge.py"
$action  = New-ScheduledTaskAction -Execute $py -Argument "`"$script`" --port 8081" -WorkingDirectory "D:\Project\ulanzi\tools\live-bridge"
$trigger = New-ScheduledTaskTrigger -AtLogOn
Register-ScheduledTask -TaskName "MultiTimerLiveBridge" -Action $action -Trigger $trigger -Description "TC002 live bridge"
```

NSSM 方式（无窗口、自动重启）：

```powershell
nssm install MultiTimerLiveBridge "C:\Path\Python\python.exe" "D:\Project\ulanzi\tools\live-bridge\bridge.py" --port 8081
nssm set MultiTimerLiveBridge AppDirectory "D:\Project\ulanzi\tools\live-bridge"
nssm set MultiTimerLiveBridge Start SERVICE_AUTO_START
nssm start MultiTimerLiveBridge
```

### 7.2 Linux systemd

```ini
# /etc/systemd/system/multitimer-live-bridge.service
[Unit]
Description=MultiTimer v3 live bridge
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/opt/multitimer-live-bridge
ExecStart=/opt/multitimer-live-bridge/.venv/bin/python bridge.py --host 0.0.0.0 --port 8081
Restart=on-failure
RestartSec=5
User=multitimer
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now multitimer-live-bridge
journalctl -u multitimer-live-bridge -f
```

### 7.3 Docker（可选，未在本机验证）

B 站/抖音版本（轻量）：

```dockerfile
FROM python:3.12-slim
WORKDIR /app
COPY requirements.txt bridge.py ./
RUN pip install --no-cache-dir -r requirements.txt
EXPOSE 8081
CMD ["python", "bridge.py", "--host", "0.0.0.0", "--port", "8081"]
```

```bash
docker build -t multitimer-live-bridge .
docker run -d --name live-bridge --restart unless-stopped \
  -p 8081:8081 multitimer-live-bridge
```

需要快手（Playwright）时，用官方镜像并在容器内装浏览器，且**必须**给足 `/dev/shm`：

```dockerfile
FROM mcr.microsoft.com/playwright/python:v1.47.0-jammy
WORKDIR /app
COPY requirements.txt bridge.py ./
RUN pip install --no-cache-dir -r requirements.txt && python -m playwright install --with-deps chromium
EXPOSE 8081
CMD ["python", "bridge.py", "--host", "0.0.0.0", "--port", "8081"]
```

```bash
docker run -d --name live-bridge --shm-size=256m --restart unless-stopped -p 8081:8081 live-bridge-ks
```

> 容器里 Playwright 版本与镜像标签请按官方文档对齐；`--shm-size` 太小会让页面崩。

## 8. 错误码表（`rooms[].error`）

| error | 含义 | 处理 |
|---|---|---|
| `timeout` | 上游超时，或 2.5 s 内没取完且无旧值 | 下一轮会再试；持续出现查网络/加大 `--timeout`、`--deadline-ms` |
| `network` | DNS/连接/TLS 失败 | 检查 PC 联网、代理、DNS |
| `http_<n>` | 上游非 200（403/429/5xx 等） | 403/429 多为风控；5xx 稍后再试 |
| `parse` | 响应不是预期 JSON/结构变了 | 平台改版，需更新 bridge；开 DEBUG 看响应码 |
| `not_found` | 房间不存在（B 站 `code 1/19002000`；抖音无数据） | 检查房间号 |
| `invalid_room` | 房间号格式不对（B 站非数字、快手非 principalId、抖音非数字） | 检查配置 |
| `unsupported_platform` | 平台名不是三家之一 | 改成 `bilibili`/`douyin`/`kuaishou` |
| `rate_limited` | 抖音 200+0B/403/429，或快手"请求过快"，或 B 站 -352 | 等 3–30 分钟；降低房间数/频率；抖音改用 19 位 `room_id` |
| `bili_code_<n>` | B 站返回了未映射的 `code` | 记录数值，更新映射 |
| `douyin_no_ttwid` | `HEAD live.douyin.com` 没拿到匿名 `ttwid`（端点或网络异常） | 稍后重试；检查 PC 能否访问 `live.douyin.com` |
| `douyin_code_<n>` | 抖音 `status_code != 0`（10011 = 参数/房间不合法） | 检查房间号形式 |
| `kuaishou_requires_browser` | 没装 Playwright | `pip install playwright && python -m playwright install chromium` |
| `kuaishou_browser_missing` | 装了包但没装 Chromium | 同上第二条命令 |
| `kuaishou_no_data` | 页面没给出 `__INITIAL_STATE__`/数据（可能是登录墙/改版） | 开 DEBUG 人工看页面；不建议为此加登录 |
| `kuaishou_browser_error` | 浏览器内部错误 | 重启 bridge；查 Chromium 依赖 |
| `too_large` | 上游响应超上限（防内存） | 平台改版或异常，记录后放宽上限 |
| `internal` | bridge 自身异常 | 看日志堆栈（`--log-level DEBUG`） |
| `response_too_large` | 顶层 `ok:false`，房间数据裁剪后仍 >4096 B | 理论上不会；用 `--title-max 0` |

## 9. 自测记录（2026-10-22 19:24–19:27，Asia/Shanghai）

环境：Windows 10、Python 3.14.7、requests 2.34.2；**未安装 Playwright**。
所有请求都是公开只读 GET；未登录、未使用任何账号；下列 `ttwid` 均未落盘/未打印。

### 9.1 离线单测

```
python -m unittest -v test_bridge
Ran 40 tests in 0.924s
OK
```

覆盖：房间参数解析/上限/非法字符、B 站 h5 与兜底解析、`code` 映射、抖音两条路径的
解析与 0 字节判定、限流熔断只发一次请求、快手状态映射、4 KB 裁剪、0 值保留、
缺字段省略、deadline 超时 + 后台回填缓存 + 旧值兜底。

### 9.2 B 站（公开房间 6，实测可用）

```powershell
curl.exe -s "http://127.0.0.1:8081/live?rooms=bilibili:6"
```

```json
{"ok":true,"ts":1792668302,"rooms":[{"key":"bilibili:6","platform":"bilibili","room":"6","ok":true,"online":197349,"likes":4044015,"fans":16896338,"status":1,"title":"【预告】24号1点全球总决赛瑞士轮"}]}
```

`Content-Length: 216`，耗时约 130 ms（冷启动首次）。同一房间 20 s 内二次请求耗时
约 13 ms（命中缓存，不再打上游）。

未开播房间（0 是有效值，实测）：

```json
{"key":"bilibili:3","platform":"bilibili","room":"3","ok":true,"online":0,"likes":0,"fans":213675,"status":0,"title":"哔哩哔哩音悦台"}
{"key":"bilibili:21452505","platform":"bilibili","room":"21452505","ok":true,"online":0,"likes":0,"fans":1116826,"status":0,"title":"我超农"}
```

不存在的房间（h5 + 兜底都试过，仍返回可解析的房间级错误）：

```json
{"key":"bilibili:999999999999","platform":"bilibili","room":"999999999999","ok":false,"error":"not_found"}
```

### 9.3 抖音（匿名，实测两条路径都通过）

19 位 `room_id`（`reflow`，无需 cookie）：

```json
{"key":"douyin:7076284313080843019","platform":"douyin","room":"7076284313080843019","ok":true,"online":0,"likes":1107004,"fans":5123123,"status":0,"title":"京东运动 「濛」力来袭"}
```

12 位 `web_rid`（网页 `enter` + 匿名 `ttwid`；注意没有 `fans`）：

```json
{"key":"douyin:611063790284","platform":"douyin","room":"611063790284","ok":true,"online":25,"likes":23561,"status":1,"title":"温柔和可爱都给你"}
```

> 这两次之后没有再压测抖音。真实限流表现是 `200`+0 字节 → `rate_limited`，
> 由离线单测（mock 空响应）覆盖；线上请按 §3.2 控制频率。

### 9.4 快手（未安装 Playwright，明确报错）

```powershell
python bridge.py --once --rooms kuaishou:3xwnf96yetb5ewm
```

```json
{"ok":true,"ts":1792668279,"rooms":[{"key":"kuaishou:3xwnf96yetb5ewm","platform":"kuaishou","room":"3xwnf96yetb5ewm","ok":false,"error":"kuaishou_requires_browser"}]}
```

退出码 `2`，`payload=166 bytes`。装上 Playwright 后的抓取路径已实现但**未在本机实测**
（本机没有 Chromium，见 §3.3 的安装命令）。

### 9.5 混合页 / 4 KB / 错误路径

```
4 个房间（B 站 + 抖音 + 快手 + 抖音）  → 690 B
8 个房间（协议上限）                    → 1344 B
2 个房间（当前设备每页大小）            → 370 B
```

```text
GET /live                                → 400 {"ok":false,"error":"missing_rooms"}
GET /live?rooms=bilibili:6;rm            → 400 {"ok":false,"error":"bad_room_key"}
GET /live?rooms=<9 个房间>               → 400 {"ok":false,"error":"too_many_rooms"}
POST /live                               → 405 {"ok":false,"error":"method_not_allowed"}
GET /nope                                → 404 {"ok":false,"error":"not_found"}
```

`GET /health`：

```json
{"ok":true,"ts":1792668302,"version":"3.0.0","uptimeSec":3,"maxRooms":8,"cachedRooms":1,"platforms":{"bilibili":"ready","douyin":"ready","kuaishou":"requires_playwright"}}
```

设备报文形状也验证过：`HTTP/1.0` + `Accept: application/json` + `Connection: close`
直接读，返回 `Content-Length: 191`、`Connection: close`、无 chunked。

## 10. 已知风险

1. **接口随时会变**：三家都没有给这类轻接口做承诺；B 站 `getInfoByRoom` 已经从可用变
   `-352`，抖音网页 `enter` 会按 IP 限流。bridge 的降级/熔断只能缓解，不能消除。
2. **抖音匿名配额**：固定公网 IP + 频繁轮询必然踩线；房间多/刷新快时优先用 19 位
   `room_id`（`reflow`），或减少抖音房间数。
3. **快手签名**：本实现不逆向 `__NS_hxfalcon`，只靠真实浏览器；浏览器被识别/页面改版时
   会退化成 `kuaishou_no_data`。这是有意为之（签名会随平台更新失效，且有合规风险）。
4. **B 站直连与 bridge 的字段一致性**：`status` 已在 bridge 侧归一化为 1/0；设备直连
   解析 `live_status` 时也要按同一语义显示，否则同一格换源会不一致。
5. **明文 + 无鉴权**：只适合可信局域网；不要暴露公网。v3.0 契约没有 token 字段。
6. **快手浏览器资源**：常驻 Chromium 约 200–400 MB 内存，小路由器/NAS 可能吃不消。

## 11. 本次交付的文件与范围

| 文件 | 说明 |
|---|---|
| `tools/live-bridge/bridge.py` | 中继主程序（HTTP server + 三平台 + 缓存/限频 + `--once`） |
| `tools/live-bridge/test_bridge.py` | 40 个离线单测（无网络、无浏览器） |
| `tools/live-bridge/requirements.txt` | `requests`；Playwright 以注释标为可选 |
| `tools/live-bridge/README.md` | 一页纸快速开始 |
| `docs/LIVE-BRIDGE.md` | 本文件（部署、边界、自测、风险） |

未改动：固件、`native/`、`web-src/`、设备、`docs/V3-CONTRACT.md` 等；
未 `git commit`；未写入任何真实 cookie/token/账号。
