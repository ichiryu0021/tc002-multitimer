# TC002（Z21 / 52×16）直播数据（B 站 / 抖音 / 快手）可行性调研

- 调研日期：**2026-10-22 18:50–19:00（Asia/Shanghai）**
- 调研性质：**只读 + 联网实测**。未修改任何源码、未改设备、未 `git commit`，仅新增本文件。
- 设备：TC002（Z21，`Zkswe_SSD21X_SPINOR`），官方固件 1.1.3，`192.168.31.124:5555`（ADB over WiFi），当前运行本仓库 MultiTimer v2。
- 目标：为"用户提供直播间号码 → 每页 4 个房间 → 指标可选"的 LED 显示功能做可行性判断。
- 数据来源：**本机 curl 实测**（Windows 10 / curl 8.21）+ **设备侧只读检查**（`ls` / `cat` / `pull` / `ping`）+ 公开资料。
- 本文所有 cookie/token/签名均已脱敏；不含任何账号凭据。

> 平台接口属于"随时会变"的外部依赖。本文结论标注了实测时间与置信度；**"今天能用"不等于"长期能用"**。

---

## 0. 结论速览

### 0.1 一句话

**B 站可以设备直连（无 cookie、无签名，一个 ~4 KB 的 GET 就能拿到标题/状态/人气/点赞/粉丝）；抖音必须 `ttwid` 且无签名请求会被快速限流（但存在一个更宽松的手机端点 `reflow/info`，缺的是 `web_rid → room_id` 映射）；快手 web 接口需要 JS 计算的 `__NS_hxfalcon` 签名，无签名稳定被拒，直连不可行。设备本身具备 TLS 条件（OpenSSL 1.1.0i + `/res/ui/cacert.pem`），但只有 35 MB 内存且没有 libcurl/头文件。因此推荐"局域网中继优先，B 站可选直连"。**

### 0.2 平台可用性总表

| 平台 | 无登录直连 | 必需 cookie | 必需签名 | 关键端点 | 建议 |
|---|---|---|---|---|---|
| **B 站** | ✅ **可行**（实测 200，~4 KB） | ❌ 不需要 | ❌ 不需要（`getH5InfoByRoom` / `get_info`） | `api.live.bilibili.com/xlive/web-room/v1/index/getH5InfoByRoom?room_id=`<br>`/room/v1/Room/get_info?room_id=` | **可设备直连**（或中继，二选一） |
| **抖音** | ⚠️ **不稳定**（先成功后限流，200 + 0 字节） | ✅ **`ttwid` 必需**（`HEAD /` 即可拿） | 网页 `enter` 无签名可短时通过；正规要求 `X-Bogus`/`a_bogus` | 网页：`live.douyin.com/webcast/room/web/enter/?...&web_rid=`<br>手机：`webcast.amemv.com/webcast/room/reflow/info/?...&room_id=` | **走中继**；直连只做"降级可用" |
| **快手** | ❌ **不可行**（稳定 `result:2` / "请求过快"） | ✅ `did` 等 | ✅ **`__NS_hxfalcon`（JS 计算）** | `live.kuaishou.com/live_api/liveroom/livedetail?principalId=` | **必须中继**（带 JS 引擎或无头浏览器），或暂不支持 |

### 0.3 指标矩阵（实测确认）

| 指标 | B 站 | 抖音 | 快手 |
|---|---|---|---|
| 直播间标题 | ✅ `room_info.title` | ✅ `title` | ⚠️ 仅首页 SSR 卡片有，单房间接口被拒 |
| 开播状态 | ✅ `live_status`（0/1） | ✅ `status`（2=直播，4=已结束） | ❌ |
| 在线/人气 | ✅ `online`（**人气值**，非并发人数） | ✅ `user_count_str`（在线观众，如 `"19"`、`"1.2万"`） | ❌（`watchingCount` 只在首页 SSR） |
| 点赞 | ✅ `like_info_v3.total_likes`（本场累计） | ✅ `like_count`（本场累计，实测 17,292 / 1,107,004） | ❌（`likeCount` 只在首页 SSR） |
| 粉丝数 | ✅ `anchor_info.relation_info.attention`（实测 16,896,341） | ✅ `owner.follow_info.follower_count`（实测 5,123,142，仅手机端点） | ❌ |
| 累计观看 | ✅ `watched_show.num`（≈人气） | ✅ `stats.total_user_str`（如 `"10万+"`） | ❌ |
| 主播昵称 | ✅ `anchor_info.base_info.uname` | ✅ `owner.nickname`（手机端点） | ❌ |
| 开播时间 | ✅ `live_start_time` / `live_time` | ✅ `create_time` / `start_time` | ❌ |

### 0.4 最重要的 5 条结论

1. **B 站是唯一"设备直连就能做"的平台。** `getH5InfoByRoom` 不需要 cookie/签名/WBI，一次请求同时给标题、开播状态、人气、点赞、粉丝；响应约 3.8–4.1 KB，实测 20 次 /10 秒无风控。注意 `getInfoByRoom`（另一个同名端点）已被风控（`code=-352`），不要用。
2. **抖音的"网页端"必须带 `ttwid`，而且无签名的请求会被快速限流。** 实测：无 cookie → `200` + **0 字节**；带 `ttwid` → 先正常（439 KB），十几次请求后再次变成 `200` + 0 字节，**25 分钟后换全新 `ttwid` 仍未恢复**（IP 级）。0 字节是它唯一的"拒绝"信号，必须当作失败处理，不能当空数据。
3. **抖音手机端点 `webcast.amemv.com/webcast/room/reflow/info/` 宽松得多**（无 cookie、37 KB、含在线/点赞/粉丝/累计观看），但它只认**数字 `room_id`**；`web_rid`（用户口中的"直播间号"）→ `room_id` 的公开映射只找到网页 `enter` 这一条路，而它正是被限流的那条。
4. **快手 web 接口需要 JS 计算的 `__NS_hxfalcon` 签名**（`app.js` 里可见 `__NS_hxfalcon=<sign>&caver=<ver>`）。实测无签名时：SSR 页面 `liveStream` 为空并附 `"请求过快，请稍后重试"`，`livedetail` 恒为 `result:2`，`websocketinfo` 也是 `result:2`；**仅 `pcConfig` 这类配置接口不需要签名**。即使提供用户 cookie 也绕不过签名，因此"用户给 cookie"对快手**无效**。
5. **设备具备 TLS 条件，但内存是硬约束。** `/lib/libssl.so.1.1` + `/lib/libcrypto.so.1.1` = **OpenSSL 1.1.0i**（无 TLS 1.3），`/res/ui/cacert.pem` 是 **145 张 CA 的 Mozilla 根证书包**，用它验证 `api.live.bilibili.com` / `live.douyin.com` / `live.kuaishou.com` 的 TLS 1.2 握手**全部通过**；官方 `stock libzkgui.so` 本身就引用 `libssl`/`libcrypto` 与 `cacert.pem`（即厂商 App 就是用这套做 HTTPS 的）。但 **MemTotal 仅 36,240 kB（约 35 MB）**、无 libcurl、无 OpenSSL 头文件，抖音那种 440 KB 响应在设备上解析有实际风险。

### 0.5 常见误解：是"需要登录账号"吗？——**不是**

结论：**抖音/快手的卡点是"浏览器环境"（逐请求 JS 签名 + 匿名 cookie + IP 信誉），不是账号身份；登录既不是必要条件，也不是充分条件。**

| 观察（本次实测） | 说明 |
|---|---|
| 抖音**无任何账号**、只带 `ttwid`（匿名设备 ID，`HEAD https://live.douyin.com/` 即可拿）→ 成功返回 `status=2`、在线 19、点赞 17,292 | "身份"不是门槛，匿名就能拿到数据 |
| 同一匿名 `ttwid`、**25 分钟后单次请求** → 仍 `HTTP 200` + 0 字节 | 当前挡路的是 **IP 级风控/配额**，不是登录态 |
| 快手带匿名 cookie（`did`/`clientid`/`kpn`…）→ 仍 `result:2` | cookie 齐了也没用，缺的是签名 |
| 快手 JS 把 `__NS_hxfalcon=<sign>&caver=<ver>` 拼在**每个请求** URL 上 | 签名逐请求现算，与登录无关 |

| 平台 | 真正缺的东西 | 登录能解决吗 |
|---|---|---|
| B 站 | 什么都不缺 | 不需要 |
| 抖音 | ① 匿名 `ttwid` ② **逐请求签名**（`X-Bogus`/`a_bogus`）③ IP 信誉 | ❌ 不能，签名是 JS 算的，cookie 替不了 |
| 快手 | ① `did` 等匿名 cookie ② **逐请求签名**（`__NS_hxfalcon` + `caver`） | ❌ 完全不能 |

- **登录唯一的用处**：当中继用**真浏览器**（自带签名）且匿名仍被风控时，登录态可能提升信任度；代价是账号风险（限流/封号），只能用中继 + 专用小号 + 可随时撤销。
- **反例**：给设备/中继塞一个登录 cookie 但不算签名 → 抖音仍可能 0 字节、快手仍 `result:2`。
- **因此**：中继 + **匿名**无头浏览器是首选；登录态只作 fallback；**绝不**把账号 cookie 放设备（`/data` 明文、复位清空）。

---

## 1. 调研方法与边界

- **联网侧**：Windows 主机 `curl 8.21.0`，真实公开房间号；只读 GET/POST，不登录、不使用任何人账号、不提交表单、不发送弹幕/点赞。
- **设备侧**：只用 `adb shell ls/cat/ps/date/ping/df`、`adb pull`（拉取到主机临时目录）做只读检查；未写设备、未重启、未改 `/res`、未改 `/data`。
- **频率控制**：B 站做了 20 次 /10 秒的限流探测；抖音/快手在发现被限流后立即停止，未做暴力重试。
- **脱敏**：`ttwid`、`did`、`buvid3/4`、`SESSDATA` 等一律写成 `<redacted>`；示例 JSON 只保留与本功能相关的字段。
- **设备时间**：设备为 UTC（实测 `Thu Oct 22 10:55:27 UTC 2026`），TLS 证书校验依赖该时间，当前正确（由 SNTP 校准）。

---

## 2. B 站直播（结论：可直连）

### 2.1 端点清单与实测结果

所有请求：`GET`，HTTPS，**无需任何 header**（实测去掉 `User-Agent`/`Referer` 仍返回 200 与完整数据；带 UA + `Referer: https://live.bilibili.com/` 更稳妥）。

| # | 端点 | HTTP | `code` | 大小 | 是否可用 |
|---|---|---|---:|---:|---|
| 1 | `/room/v1/Room/get_info?room_id=6` | 200 | 0 | 1,718 B | ✅ |
| 2 | `/xlive/web-room/v1/index/getH5InfoByRoom?room_id=6` | 200 | 0 | 3,851–4,059 B | ✅ **推荐** |
| 3 | `/room/v1/Room/room_init?id=6` | 200 | 0 | 311 B | ✅（短号→真号） |
| 4 | `/room/v1/Room/getRoomInfoOld?mid=50329118` | 200 | 0 | 6,590 B | ✅（字段少，不推荐） |
| 5 | `/live_user/v1/UserInfo/get_anchor_in_room?roomid=6` | 200 | 0 | 371 B | ✅（只有主播资料，无数据） |
| 6 | `/room/v1/Room/get_status_info_by_uids?uids[]=50329118&uids[]=2` | 200 | 0 | 1,611 B | ✅（**按 UID 批量**，需先知道 UID） |
| 7 | `/xlive/web-room/v1/index/getInfoByRoom?room_id=6` | 200 | **-352** | 39 B | ❌ 风控 |
| 8 | `/xlive/web-interface/v1/second/getList?...`（直播列表） | 200 | **-352** | 39 B | ❌ 风控 |

> 结论：**第 2 条（`getH5InfoByRoom`）是唯一"一次给全指标"的端点**；第 1 条（`get_info`）作为最稳定兜底（含 `attention` 粉丝数）；第 3 条用于把用户输入的短号/真号统一。

### 2.2 `get_info`：最稳的兜底

```http
GET https://api.live.bilibili.com/room/v1/Room/get_info?room_id=6 HTTP/1.1
Host: api.live.bilibili.com
User-Agent: Mozilla/5.0 ...
Referer: https://live.bilibili.com/
```

实测响应（2026-10-22 19:0x，已裁剪；room_id=6 是"哔哩哔哩英雄联盟赛事"官方直播间）：

```json
{
  "code": 0, "msg": "ok",
  "data": {
    "uid": 50329118,
    "room_id": 7734200,
    "short_id": 6,
    "attention": 16896341,        // 粉丝数
    "online": 193498,             // 人气值（不是并发人数）
    "live_status": 1,             // 0 未开播 / 1 直播中
    "title": "【预告】24号1点全球总决赛瑞士轮",
    "area_name": "游戏赛事",
    "parent_area_name": "赛事",
    "live_time": "2026-10-15 15:50:15",
    "is_portrait": false,
    "keyframe": "https://i0.hdslb.com/.../keyframe...jpg",
    "user_cover": "https://i0.hdslb.com/.../cover.jpg"
  }
}
```

- 传 `short_id`（6）或真 `room_id`（7734200）都能查到；响应里的 `room_id` 是"归一化后的真号"。
- **错误形态**：不存在的房间 → HTTP 200 + `{"code":1,"msg":"未找到该房间","data":[]}`；非法参数 → `{"code":1,"msg":"invalid params"}`。**HTTP 状态码永远是 200，必须看 `code`**。

### 2.3 `getH5InfoByRoom`：一次拿全（推荐）

```http
GET https://api.live.bilibili.com/xlive/web-room/v1/index/getH5InfoByRoom?room_id=6 HTTP/1.1
User-Agent: Mozilla/5.0 ...
```

实测响应（裁剪，保留与本功能相关字段）：

```json
{
  "code": 0,
  "data": {
    "room_info": {
      "uid": 50329118, "room_id": 7734200,
      "title": "【预告】24号1点全球总决赛瑞士轮",
      "live_status": 1,
      "live_start_time": 1792050615,
      "online": 193498,
      "area_name": "游戏赛事", "parent_area_name": "赛事",
      "keyframe": "https://i0.hdslb.com/.../keyframe....jpg"
    },
    "anchor_info": {
      "base_info": { "uname": "哔哩哔哩英雄联盟赛事", "face": "https://i2.hdslb.com/..." },
      "relation_info": { "attention": 16896341 }
    },
    "watched_show": { "num": 193498, "text_small": "19.3万", "text_large": "19.3万人气" },
    "like_info_v3": { "total_likes": 4042844 }
  }
}
```

- **点赞** `like_info_v3.total_likes` 是**本场累计**：同一房间实测 4,042,844 → 4,042,958（在涨）；未开播房间为 `0`。
- **粉丝** `anchor_info.relation_info.attention` 与 `get_info.data.attention` 一致（16,896,341）。
- **在线** `room_info.online` 与 `watched_show.num` 相同，都是**人气值**；`watched_show.text_large` 直接给出 `"19.3万人气"` 这种可显示文本。
- 未开播房间（实测 room 3 / 5050 / 21452505）：`live_status=0`、`online=0`、`total_likes=0`，但 `title`、`attention` 仍有值。
- **错误形态**：不存在的房间 → HTTP 200 + `{"code":19002000,...}`。

### 2.4 为什么不要用 `getInfoByRoom`

`/xlive/web-room/v1/index/getInfoByRoom?room_id=6` 实测返回 **`{"code":-352,"msg":"-352"}`（39 字节）**。

- 追加 `buvid3`/`buvid4`（由公开接口 `https://api.bilibili.com/x/frontend/finger/spi` 取得）后**仍然是 -352**。
- -352 是 B 站风控（通常需要 WBI 签名 `w_rid`/`wts` + 更完整的 cookie 组合）。同类被风控的还有直播列表接口。
- **含义**：B 站"页面级"接口已经开始收紧，但 `get_info` / `getH5InfoByRoom` 这两个"轻接口"目前仍开放。选型上要优先用轻接口，并且**做好它某天也被 -352 的降级方案**（降级到 `get_info`，它连 `Referer` 都不需要）。

### 2.5 cookie / 登录 / 签名 / WBI 结论

| 项目 | 结论 | 证据 |
|---|---|---|
| cookie | **不需要** | 无 cookie、无 UA、无 Referer 均 200 且字段齐全 |
| 登录 | **不需要** | 同上 |
| WBI 签名 | **`get_info` / `getH5InfoByRoom` 不需要** | 直接 200；`getInfoByRoom` 才 -352 |
| 频率限制 | 宽松 | 20 次 /10 秒（300 ms 间隔）**全部成功**，无 412 |
| 明文 HTTP | ❌ 强制 HTTPS | `http://api.live.bilibili.com/...` → **302** → `https://...`；`http://live.bilibili.com/6` → 302 |

### 2.6 报文细节（对设备实现有影响）

```
HTTP/1.1 200 OK
Content-Type: application/json; charset=utf-8
Transfer-Encoding: chunked          ← 注意：没有 Content-Length
Connection: keep-alive
Cache-Control: no-cache
```

- **B 站这个接口是 chunked**，即使客户端发 `Connection: close` 也还是 chunked。
- 但发 **HTTP/1.0** 请求时，返回 `Connection: close`、无 `Content-Length`、无 chunked → **读到 EOF 即结束**，实现最简单。
- 不带 `Accept-Encoding: gzip` 时返回明文；带了才 gzip。**设备端不要发 `Accept-Encoding`**，省掉解压。
- 响应体积 ~4 KB，对 35 MB 内存的设备完全可接受。

### 2.7 指标语义（容易误解，必须写进 UI 说明）

- **`online` 是"人气值"**，不是并发观看人数。B 站网页上显示的也是这个数（`"19.3万人气"`）。真实并发人数不公开。
- `attention` 是**主播粉丝数**（不是直播间观众）。
- `total_likes` 是**本场累计点赞**，开播前为 0，跨场会重置。

### 2.8 B 站侧风险

1. **接口无文档**：`getH5InfoByRoom` 是网页内部接口，字段可能增删；`code` 语义可能变。
2. **风控扩散**：`getInfoByRoom` 已经从"可用"变成 -352，说明 B 站会收紧。建议实现里内置"端点健康度"：主用 h5，连续失败 N 次自动降级到 `get_info`。
3. **短号/真号**：用户可能给 `6` 这种短号；`room_init?id=` 可以归一化（实测 200/311 B）。

---

## 3. 抖音直播（结论：直连不稳定，需中继）

### 3.1 两种"房间号"

| 概念 | 形态 | 用户是否知道 | 用途 |
|---|---|---|---|
| `web_rid` | 12 位数字（如 `611063790284`） | ✅ 就是分享链接 `live.douyin.com/<web_rid>` 里的号 | 网页端入口 |
| `room_id` | 19 位数字（如 `7076284313080843019`） | ❌ 普通用户看不到 | 手机端 `reflow/info` 必需 |
| `owner.web_rid` | 12 位数字 | — | 主播的 web_rid（不是房间） |

**核心难点**：手机端点只认 `room_id`，而用户只给 `web_rid`；`web_rid → room_id` 的映射只找到网页 `enter` 一条路，而它正好是被限流的那条。

### 3.2 网页 `enter` 端点：能用，但要 `ttwid`，且会被限流

```http
GET https://live.douyin.com/webcast/room/web/enter/?aid=6383&app_name=douyin_web&live_id=1
    &device_platform=web&language=zh-CN&enter_from=web_live&cookie_enabled=true
    &screen_width=1920&screen_height=1080&browser_language=zh-CN&browser_platform=Win32
    &browser_name=Mozilla&browser_version=5.0&web_rid=<web_rid> HTTP/1.1
Host: live.douyin.com
User-Agent: Mozilla/5.0 ...
Cookie: ttwid=<redacted>
Referer: https://live.douyin.com/<web_rid>
```

**`ttwid` 怎么来（实测）**：`HEAD https://live.douyin.com/`（注意：返回 **404**，但 `Set-Cookie` 里带 `ttwid`），**不需要下载 945 KB 首页 HTML**。这对设备很关键。

```http
HTTP/1.1 404 Not Found
Set-Cookie: ttwid=1%7C<redacted>%7C<ts>%7C<sig>; Domain=.douyin.com; Path=/; HttpOnly; SameSite=None; Secure=true
```

实测成功样本（2026-10-22 19:0x，已脱敏/裁剪）：

```json
{
  "status_code": 0,
  "data": {
    "data": [{
      "id_str": "7401...",                  // ← 数字 room_id，手机端点要用它
      "status": 2,                          // 2=直播中，4=已结束
      "title": "温柔和可爱都给你",
      "user_count_str": "19",               // 在线观众（"万"级会写 "1.2万"）
      "like_count": 17292,                  // 本场累计点赞
      "owner_user_id_str": "..."
    }],
    "similar_rooms": [ { "web_rid": "548507406983" }, ... ]   // 12 个相似房间，可当"发现"
  },
  "extra": { "now": 1792666359720 }
}
```

**失败形态（非常重要）**：

| 场景 | 结果 |
|---|---|
| 不带 cookie | `HTTP 200` + **0 字节** |
| `ttwid` 失效/被限流 | `HTTP 200` + **0 字节** |
| 不存在的 `web_rid` | `HTTP 200` + **0 字节** |
| 房间已结束 | 正常 JSON，`status=4`、`user_count_str="0"` |

→ **"200 + 空 body" 是抖音的统一拒绝信号**，必须当作"失败"而不是"空数据"，否则 UI 会显示成 0 在线。

**限流实测（关键）**：十几次请求（含少量无效 `web_rid` 探测）之后，所有 `enter` 请求变成 `200` + 0 字节；**25 分钟后换一个全新 `ttwid` 做单次请求，仍然是 0 字节** → 说明这是 **IP 级配额/信誉**，不是 cookie 级。4 个房间 × 30 秒轮询 = 8 次/分钟，**用不了几分钟就会踩线**。

**响应体积**：单个房间 **409–480 KB**（大量 AB 配置、推流地址、礼物配置）。这是设备侧最大的内存风险点。

### 3.3 手机端点 `reflow/info`：小、稳、字段全，但要 `room_id`

```http
GET https://webcast.amemv.com/webcast/room/reflow/info/?type_id=0&live_id=1
    &room_id=<19位数字 room_id>&aid=1128&app_name=aweme&app_id=1128
    &device_platform=android&version_code=290100 HTTP/1.1
User-Agent: Mozilla/5.0 (Linux; Android 9; ...) ...
```

- **不需要 cookie、不需要签名**（实测无 cookie 直接 200）。
- 响应 **36–37 KB**，`Content-Length` 明确，非 chunked。
- 实测样本（已结束房间，字段结构就是直播时的结构）：

```json
{
  "status_code": 0,
  "data": {
    "room": {
      "id_str": "7076284313080843019",
      "status": 4,                          // 2 直播中 / 4 已结束
      "title": "京东运动 「濛」力来袭",
      "user_count": 0,                      // 当前在线（直播时非 0）
      "like_count": 1107004,                // 本场累计点赞
      "stats": {
        "total_user_str": "10万+",          // 累计观看
        "user_count_str": "0",
        "follow_count": 1044
      },
      "owner": {
        "nickname": "王濛",
        "follow_info": { "follower_count": 5123142 },
        "web_rid": "<anchor web_rid>"
      }
    }
  }
}
```

- `app_id` / `aid` **必须都带**（缺一个会返回 `status_code:10011`，`"app id:0 and live id:1 mismatch"`）。
- 传 `room_id=0&web_rid=xxx` → `status_code:10011` + `"room_id invalid"`（不认 `web_rid`）。
- 该端点在网页 `enter` 被限流期间**仍然可用**（实测 19:1x 依旧 200 / 36,792 B），说明它走的是另一套风控。

### 3.4 `web_rid → room_id` 的映射尝试（都失败）

| 尝试 | 结果 |
|---|---|
| `reflow/info?...&web_rid=<web_rid>`（无 `room_id`） | `10011` `"room_id invalid"` |
| `reflow/info?...&room_id=0&web_rid=<web_rid>` | `10011` `"room_id invalid"` |
| `webcast.amemv.com/webcast/room/info/?web_rid=` | `200` + 0 字节 |
| `webcast.amemv.com/webcast/room/web/enter/?web_rid=` | `{"status_code":0,"status_msg":"url doesn't match"}` |
| 房间页 HTML `live.douyin.com/<web_rid>` | 928 KB 的 SPA 壳，**不含房间数据/`web_rid`**（正则 0 命中） |

→ **结论：映射只能靠网页 `enter`**（也就是会被限流的那条），或者用带签名的第三方实现。

### 3.5 抖音的签名机制（为什么不建议在设备上做）

- 网页端正规链路需要 `X-Bogus` / `a_bogus`。开源实现（`f2`）的做法是：
  1. 拼一段固定顺序的字符串（`live_id=1,aid=6383,version_code=...,webcast_sdk_version=...,room_id=...,user_unique_id=...`）；
  2. **MD5** 得到 `X-MS-STUB`；
  3. 把 `X-MS-STUB` 丢进一段**高度混淆的 JS**（`webcast_signature.js`）执行，得到 `X-Bogus`。
- 也就是说：**签名算法在 JS 里，且随平台更新**。设备端 C++ 要么内嵌一个 JS 引擎（不可行：内存/体积），要么把 JS 逻辑逆向成 C++（易失效、法律风险高）。
- 这也是为什么"直连抖音"的工程成本远高于 B 站。

### 3.6 抖音的替代方案评估

| 方案 | 可行性 | 说明 |
|---|---|---|
| **局域网中继（推荐）** | ✅ 高 | 中继用 Node/Python 跑签名库或无头浏览器，设备只取简单 JSON |
| 中继 + 无头浏览器（Playwright） | ✅ 高但重 | 最稳，能覆盖签名叫更新；一台常开 PC/NAS 可承受；资源占用 ~300–500 MB |
| 中继 + `f2` 类签名库 | ⚠️ 中 | 轻量，但签名 JS 更新后要跟着升级 |
| 设备直连 `reflow` + 手工填 `room_id` | ⚠️ 低 | 字段全、体积小，但要求用户提供 19 位数字 ID，体验差 |
| 用户提供 cookie | ❌ 无效 | cookie 不能替代签名；且涉及账号安全，不建议 |
| 第三方聚合 API | ⚠️ 未知 | 多为付费/不稳定/合规风险，未验证，不建议依赖 |

### 3.7 抖音结论

- **"能拿到"是事实**：实测拿到过 `user_count_str`、`like_count`、`title`。
- **"能稳定拿到"是否定的**：无签名 + 固定 IP + 4 房间轮询 → 很快进入长时间 200/0B。
- **推荐**：抖音**不做设备直连**，统一走中继；设备侧只保留一条"如果中继不可用则显示 `--`"的降级路径。

---

## 4. 快手直播（结论：直连不可行，必须中继）

### 4.1 房间号体系

- 快手直播间的 URL 是 `https://live.kuaishou.com/u/<principalId>`，`principalId` 形如 `3xwnf96yetb5ewm`（**字符串**，不是纯数字）。
- 用户口中的"快手房间号"通常是 `principalId`，也可能是数字 `userId`；两者不能混用（`livedetail` 只认 `principalId`）。
- 实测从快手首页 SSR 里能直接抓到 5 个带观看人数的 `principalId`（`3xwnf96yetb5ewm`、`3xfrjft92rsvets`、`3xyyxg9juzab3a6`、`3xve6v4vmy22h9w`、`3x63vdhcrzk48c4`）；同一份首页数据里这些卡片显示 `watchingCount` = 1376 / 97 / 63 / 98 / 93、`likeCount` = 7322 / 0 / 1016 / — / 15（字段确实存在，见 §4.5）。

### 4.2 单房间 SSR 页面：被降级

```http
GET https://live.kuaishou.com/u/3xwnf96yetb5ewm HTTP/1.1
```

实测（200，57,569 B）内嵌 `window.__INITIAL_STATE__` 中：

```json
"liveroom": {
  "playList": [{
    "liveStream": {},
    "author": {},
    "isLiving": false,
    "errorType": { "type": 2, "title": "请求过快，请稍后重试", "content": "浏览其他内容" }
  }],
  "websocketUrls": [], "token": ""
}
```

- **没有任何 `watchingCount` / `likeCount`**；即使这些房间在首页显示 1376 人在看。
- 带 `Cookie: did=...`、完整浏览器头、`?fid=0&cc=share_copylink` 都一样。
- 近 20 分钟后重试仍然 `"请求过快"` → **不是短时限流，是持续拒绝**。

### 4.3 `livedetail` API：接口存在，但恒被拒

```http
GET https://live.kuaishou.com/live_api/liveroom/livedetail?principalId=3xwnf96yetb5ewm
Referer: https://live.kuaishou.com/u/3xwnf96yetb5ewm
```

实测（200，634 B，`Content-Length` 明确）：

```json
{
  "data": {
    "result": 2,
    "liveStream": { "playUrls": {"h264":{},"hevc":{}}, "url": "https://m.gifshow.com/fw/live/undefined", "type": "live" },
    "author": { "living": false, "counts": {} },
    "websocketInfo": {},
    "config": { "canSendGift": false }
  }
}
```

- `result: 2` 就是"请求过快/被拒"的返回码（与 SSR 的 `errorType.type=2` 对应）。
- `websocketinfo` 同样 `{"data":{"result":2}}`。
- 只有配置类接口是开放的：`POST /live_api/web/pcConfig` → `result:1` + 完整配置（**不需要签名**）。
- `GET /live_api/liveroom/status`、`/live_api/baseuser/userinfo` 返回 **405**（要求 POST），未继续深挖。

### 4.4 签名证据：`__NS_hxfalcon`

从快手自己的 JS bundle（`p2-game.kskwai.com/udata/pkg/KS-GAME-WEB/pc-live-next/js/app.67dfb3d67effc6a46f6b.js`）里可以直接读到：

```js
return {
  params: _,
  url: "".concat(t).concat(c, "__NS_hxfalcon=").concat(u, "&caver=").concat(o)
}
```

- `u` 来自一个 **签名服务**（`h(a)` 调用返回 `s.signResult`），`caver` 是 `$getCatVersion` 的版本号。
- 同文件里还有一整套混淆过的签名 VM（`n.prototype.register`，`Object.defineProperties(Object,{jmpOnw_b2sa:..., jmpOnw_...})`，常量里带 `O="__NS"`）。
- 也就是说：**快手的签名是"每次请求现算"的 JS 代码，不是固定算法**。

补充证据：快手配置里 `"pcLive.webConfig.needShieldWatchingCount": 1` —— 平台本身就有"隐藏观看人数"的开关，说明观看数不是无条件公开字段。

### 4.5 快手结论

- **无签名直连：不可行**（SSR 空、`livedetail` 恒 `result:2`、`websocketinfo` 同样）。
- **"用户给 cookie" 也无效**：签名是逐请求的，cookie 只是身份，不是签名。
- **可行路径**：中继侧用 Node + 快手签名库（社区有 `__NS_sig3`/`__NS_hxfalcon` 实现，但**随版本失效**）或无头浏览器直接打开房间页取 `watchingCount`。
- **建议**：一期**不做快手直连**；中继里把快手标为"实验性/可关闭"，并在 UI 上明确"数据可能不可用"。

---

## 5. 设备侧可行性（TLS / CA / 库 / 内存）

### 5.1 硬件与内存（实测）

```
MemTotal:       36,240 kB      ← 约 35 MB，这是硬上限
MemFree:         4,824 kB
MemAvailable:   17,452 kB
SwapTotal:           0 kB

processor  : 0 / 1
model name : ARMv7 Processor rev 5 (v7l)
Features   : half thumb fastmult vfp edsp thumbee neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm
CPU part   : 0xc07            ← Cortex-A7
Hardware   : SStar Soc (Flattened Device Tree)
```

- **双核 Cortex-A7，ARMv7 hard-float（armhf），NEON，无 swap。**
- 现有 `zkgui` 进程 VSZ 约 142 MB（共享库占大头），`MemAvailable` 只有 ~17 MB。
- **含义**：4 KB（B 站）/37 KB（抖音手机端）响应可以处理；**439–480 KB（抖音网页 enter）在设备上解析风险很高**（一次 malloc + 字符串副本 + JSON 树很容易吃掉几 MB 并碎片化）。

### 5.2 库与 TLS 现状（`ls` 实测）

| 项目 | 结果 |
|---|---|
| `/lib/libssl.so` → `libssl.so.1.1` | ✅ 235,776 B |
| `/lib/libcrypto.so` → `libcrypto.so.1.1` | ✅ 875,672 B |
| **OpenSSL 版本** | **1.1.0i**（`libssl` 与 `libcrypto` 内部字符串一致） |
| `libcurl` | ❌ 不存在 |
| `curl` / `wget` / `openssl` 命令行 | ❌ 不存在（`busybox` 是裁剪版，无 `wget`，`--list` 都没有） |
| glibc | 2.30（`libc-2.30.so`、`ld-2.30.so`） |
| `/usr/lib`、`/system/bin` | ❌ 不存在（rootfs 极简：`/lib` + `/bin`） |

**OpenSSL 1.1.0i 的能力边界**：
- ✅ TLS 1.2（含 ECDHE-RSA、AES-GCM、ChaCha20-Poly1305）、SNI、ALPN、证书链校验
- ❌ **TLS 1.3**（1.1.1 才有）
- ⚠️ 2018 年的版本，已 EOL；但对"客户端连大厂 HTTPS"够用
- 实测（主机 OpenSSL 用同一份 CA、强制 TLS1.2）：`api.live.bilibili.com` / `live.bilibili.com` / `live.douyin.com` / `live.kuaishou.com` / `webcast.amemv.com` **全部 TLS1.2 握手成功**，协商到 `ECDHE-RSA-CHACHA20-POLY1305` 或 `ECDHE-RSA-AES128-GCM-SHA256`（都是 1.1.0i 支持的套件）。

### 5.3 CA 证书（关键发现）

| 项目 | 结果 |
|---|---|
| `/res/ui/cacert.pem` | ✅ 存在，**226,168 B**，**145 张证书**，Mozilla 根证书包（`## Bundle of CA Root Certificates`） |
| `/etc/ssl` | ❌ 不存在（系统没有其它 CA 路径） |
| 用该文件验证三个域名 | ✅ `api.live.bilibili.com` / `live.douyin.com` / `live.kuaishou.com` **全部 `ssl_verify=0`**（OpenSSL 校验通过） |

**厂商 App 自己就是这么干的（硬证据）**：

```
build/device-backup/stock-libzkgui.so：
  "libssl.so"    出现 1 次   ← 动态依赖库名（与 DT_NEEDED 一致）
  "libcrypto.so" 出现 1 次   ← 动态依赖库名（与 DT_NEEDED 一致）
  "cacert.pem"   出现 3 次
  "SSL_CTX"      出现 207 次
  "BIO_new"      出现 14 次
dist/lib/libzkgui.so（我们现在的）：以上全部 0 次
```

→ **官方固件里的 `libzkgui.so` 本身就依赖 OpenSSL、并用 `/res/ui/cacert.pem` 做 HTTPS**。也就是说这条链路在真机上是**被验证过的**，不是理论可行。我们只是还没用它。

### 5.4 明文 HTTP 能不能用？——不能

| 目标 | 明文 HTTP 结果 |
|---|---|
| `http://api.live.bilibili.com/room/v1/Room/get_info` | **302** → `https://...` |
| `http://live.bilibili.com/6` | **302** → `https://...` |
| `http://live.douyin.com/`（及任意路径） | **301** → `https://...` |
| `http://live.kuaishou.com/`（及 API） | **307** → `https://...` |

→ **三家全部强制 HTTPS**，设备必须实现 TLS。没有"用明文 HTTP 绕过"的选项。

### 5.5 报文细节（决定 HTTP 客户端怎么写）

| 主机 | 分块 | Content-Length | HTTP/1.0 是否可用 |
|---|---|---|---|
| `api.live.bilibili.com`（h5 接口） | ✅ **chunked**（带 `Connection: close` 仍 chunked） | ❌ 无 | ✅ 返回 `Connection: close` + 无长度（读到 EOF） |
| `webcast.amemv.com`（reflow） | ❌ | ✅ 36,792 B | ✅ |
| `live.kuaishou.com`（livedetail） | ❌ | ✅ 634 B | ✅ |

**实现建议**：写一个 HTTP/1.1 客户端，**必须支持 chunked**（B 站）；或者发 `HTTP/1.0` + 读到 EOF（三家都接受，最省事，但部分 CDN 未来可能拒绝 1.0，建议 chunked 和 close-delimited 都实现，共约 60 行）。

### 5.6 直连 TLS 的实现路径与工作量

SDK 依赖（`D:\zkswe\FlyThingsIDE\bin\.dep\public\Z21`）里：

- ❌ **没有** OpenSSL 头文件、没有 `libssl.so`/`libcrypto.so`、没有 curl、没有 HTTP 客户端头。
- ✅ 有 `base-json`（`base/json_object.h`、`base/json_array.h`，`libbase-json.a`）、`libz.a`（zlib）、`base/base64.h`、`base/crypto.h`（**只有 MD5**，`hashString(HASH_MD5, ...)`，注释写明"依赖 openssl"；`libbase-utility.a` 的字符串里能搜到 `MD5_Init/MD5_Update/MD5_Final`，说明这些符号要靠外部 libcrypto 解析）。

因此有三条路：

| 方案 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| **A. `dlopen` 运行时加载（推荐）** | `dlopen("libssl.so.1.1")` + `dlsym` 取 `SSL_CTX_new`/`TLS_client_method`/`SSL_connect`/`SSL_read`/`SSL_write`/`SSL_CTX_load_verify_locations` 等约 20 个符号；自己写 20 行函数指针声明 | **不需要头文件、不需要链接期 .so**；官方设备上库一定存在 | 符号名写错要到运行时才发现；需处理 `SSL_get_error` |
| B. 自备链接桩 | 手写 `openssl_stub`（空的 `.so`/`.a` 只导出符号）加入工程，链接期用 `-lssl -lcrypto`，运行时用设备真库 | 代码更"正常" | 要维护桩；IDE 链接参数要改；容易和 IDE 的链接脚本打架 |
| C. 自带 TLS 库（mbedTLS） | 交叉编译 mbedTLS 静态库塞进 `/res/lib` | 不依赖系统 OpenSSL，可用新 TLS | 体积 +~300 KB（`/res` 8 MiB 够），但要自己接 CA、熵源、SNI；工作量大 |

**推荐 A**：工作量约"1 个 TLS 封装类（~300 行）+ 1 个 HTTP/1.1 客户端（~400 行）+ 4 KB JSON 解析（复用现有 `mini_json`）"。

**注意**：`/lib/libssl.so` 是**无版本符号链接**（`libssl.so -> libssl.so.1.1`），`dlopen("libssl.so.1.1")` 最稳；`libssl.so.1.1` 自己会 `NEEDED libcrypto.so.1.1`，加载器会带进来。

### 5.7 内存/线程评估

- TLS 上下文（`SSL_CTX` + `SSL` + 证书链）：**估算**约 **200–400 KB** 常驻；握手瞬时再 +100 KB。35 MB 设备**可以承受**，但要**全局只建一个 `SSL_CTX`**，CA 只加载一次（145 张证书解析后**估算**约 150–250 KB）。
- **JSON 解析**：B 站 4 KB 没问题；抖音 440 KB 不建议（峰值可能 3–5 MB）。抖音手机端 37 KB 可接受。
- **线程**：现有 App 已经是 `std::thread(...).detach()` 风格（`HttpServer`、`WifiBootstrap` 都这样），加一个 `LivePoller` 线程完全符合现有架构；**不要**在主渲染线程里做网络。
- **不要在设备上并发拉 4 个房间**：串行 + 每房间间隔 150–300 ms，峰值内存和 TLS 握手开销都可控。
- **超时**：connect 5 s / 总 8 s；失败重试 2 次（2 s、6 s 退避）。

### 5.8 构建与镜像约束

- `/res` 分区 **8 MiB**，当前 v2.1.2 镜像 payload 761,856 B（9.08%），加 ~100 KB 代码没问题；但**不要**把 CA 再拷一份（已有 `/res/ui/cacert.pem`）。
- 工程平台必须是 Z21；`Main.cpp` 的 `sys.zkapp.state=running` 不能动（防砖）。
- `dlopen` 需要 `-ldl`（glibc 2.30 的 `libdl.so.2` 存在）。

---

## 6. 推荐架构

### 6.1 直连 vs 中继

| 维度 | 设备直连 | 局域网中继 |
|---|---|---|
| B 站 | ✅ 可行（4 KB，无需 cookie） | ✅ 更简单 |
| 抖音 | ❌ 不稳定（ttwid + 限流 + 440 KB） | ✅ 可跑签名/浏览器 |
| 快手 | ❌ 需要 JS 签名 | ✅ 可跑签名/浏览器 |
| 设备工作量 | TLS + HTTP + JSON + 各平台适配（大） | 只要 HTTP + JSON（小） |
| 内存峰值 | 抖音 440 KB 风险 | 设备侧 <10 KB |
| 失效风险 | 集中在设备固件里，改一次要重新刷机 | 中继可随时更新，设备不用动 |
| 额外硬件 | 无 | 需要一台常开 PC/NAS/HA |
| 合规 | 设备直接爬，风险在固件里 | 风险集中在中继，可随时关停 |

**推荐：中继优先（抖音/快手必须），B 站"中继优先、直连可选"。**

理由：
1. 抖音/快手的签名是 JS 且会变，只有中继能跟得上；
2. 设备只有 35 MB 内存，440 KB 响应是真实风险；
3. 中继让"改逻辑"不需要重新刷 `/res`（现在的固化流程很重）；
4. 设备侧代码量可以从 ~1500 行降到 ~400 行。

### 6.2 中继提供的协议（建议）

设备 → 中继，**局域网明文 HTTP**（中继在可信网段；如不放心可加一个共享 token）：

```http
GET /api/live?platform=bili&rooms=6,5440,7734200,3 HTTP/1.1
Host: 192.168.31.10:8099

HTTP/1.1 200 OK
Content-Type: application/json

{
  "ts": 1792666800,
  "items": [
    { "platform":"bili", "room":"6", "ok":true, "live":true,
      "title":"【预告】24号1点全球总决赛瑞士轮",
      "online":193498, "likes":4042958, "fans":16896342,
      "watched":193498, "updated":1792666790 },
    { "platform":"douyin", "room":"611063790284", "ok":false,
      "error":"throttled", "updated":0 }
  ]
}
```

- 字段固定、体积小（4 个房间 < 2 KB），设备解析零压力。
- `ok:false` + `error` 让设备显示 `--` 而不是 0。
- 中继内部再按平台做限频/缓存（同一房间 30 s 内复用上次结果）。

### 6.3 设备侧适配层（如果坚持直连，或做"中继不可用时的 B 站兜底"）

```cpp
// net/live/LiveTypes.h
struct LiveMetrics {
    bool     valid = false;     // 是否拿到过有效数据
    bool     live = false;      // 是否在播
    long     online = 0;        // 在线/人气
    long     likes = 0;         // 点赞（本场累计）
    long     fans = 0;          // 粉丝
    long     watched = 0;       // 累计观看（可选，-1 表示无）
    char     title[64] = {0};   // UTF-8，截断
    long     updatedAt = 0;     // 本地 epoch
    char     error[24] = {0};   // "timeout" / "http" / "parse" / "auth"
};

// net/live/ILiveProvider.h
class ILiveProvider {
public:
    virtual ~ILiveProvider() {}
    // 同步调用，内部超时；返回 false 时 metrics.error 有原因
    virtual bool fetch(const char* roomNo, LiveMetrics& out) = 0;
    virtual const char* name() const = 0;
};
```

- `BiliProvider`：`getH5InfoByRoom` → 失败降级 `get_info`（无点赞）。
- `RelayProvider`：`GET http://<relay>/api/live?...`，一个请求拿 4 个房间。
- 统一由 `LivePoller`（单线程、串行、30–60 s）驱动，结果写入 `std::mutex` 保护的快照，渲染线程只读快照。
- **渲染永远不阻塞**：拿不到就显示 `--`，并保留上一次好数据 + `"N 分钟前"` 标记（可选）。

### 6.4 每平台支持的指标（建议 UI 只显示这些）

| 指标 | B 站 | 抖音（中继） | 快手（中继，实验性） |
|---|---|---|---|
| 标题 | ✅ | ✅ | ⚠️ |
| 开播/离线 | ✅ | ✅ | ⚠️ |
| 在线/人气 | ✅ 人气 | ✅ 在线观众 | ⚠️ |
| 点赞 | ✅ | ✅ | ⚠️ |
| 粉丝 | ✅ | ✅ | ⚠️ |
| 累计观看 | ✅（=人气） | ✅ | ⚠️ |

> 52×16 的屏，一页 4 个房间，建议每行：`[序号] 平台图标 + 数值(4位) + 单位`，超长标题滚动或不显示。

### 6.5 超时 / 重试 / 限频 / 失败占位

| 项 | 建议值 | 说明 |
|---|---|---|
| 连接超时 | 5 s | 设备 32 ms RTT，5 s 足够 |
| 总超时 | 8 s | 含 TLS 握手 |
| 重试 | 2 次（2 s、6 s 退避） | 只对网络/5xx 重试，`code!=0` 不重试 |
| 轮询间隔 | 30–60 s（带 ±10% 抖动） | 4 房间 = 4–8 请求/分钟 |
| B 站限频 | 每房间 ≥10 s，全局 ≤1 req/s | 实测 20 req/10 s 无风控 |
| 抖音限频 | 中继侧 ≥60 s/房间，且严格缓存 | 无签名直连十几次就踩线 |
| 快手限频 | ≥60 s/房间 | 无签名恒失败，有签名也要保守 |
| 失败占位 | `--`（不显示 0） | 0 是有效值（离线房间），必须区分 |
| 陈旧数据 | >3 个周期未更新 → 变暗/加 `?` | 避免"假在线" |

### 6.6 凭据 / cookie 存储

| 数据 | 是否需要 | 建议存放 |
|---|---|---|
| B 站 | 不需要 | — |
| 抖音 `ttwid` | 需要（直连时） | `/data/live_douyin.ck`，`0600`，只存 `ttwid` |
| 快手 `did` / 签名 | 需要（中继侧） | **只放中继**，不要下发到设备 |
| 用户账号 cookie | **不建议** | 若用户坚持：只存中继，文件 `0600`，不进 git、不进日志、不打印 |
| 中继共享 token | 可选 | `/data/multitimer.conf` 旁边，`0600` |

注意：设备 `/data` 是 **JFFS2 明文**，复位键会清空；不要把任何账号 cookie 放设备。

### 6.7 合规与失效风险

**合规**：
- 三家平台的用户协议都限制"未经授权的数据抓取"；本功能应定位为**个人、非商业、本地显示**。
- **不绕过登录、不绕过风控、不存储个人信息、不对外提供数据、不做二次分发**。
- 若要商用/公开，必须走官方开放平台（B 站直播开放平台、抖音开放平台、快手开放平台），需要主体资质与授权，成本远高于本方案。
- 建议在配置页写明数据来源与"可能中断"的免责说明。

**失效风险（按概率排序）**：
1. **快手签名升级** → 中继的签名实现失效（高概率，社区库通常几周～几月一更新）。
2. **抖音风控加强**（无签名彻底不可用） → 中继必须上无头浏览器（中概率）。
3. **B 站 `getH5InfoByRoom` 也 -352** → 降级 `get_info`（丢点赞），或改走中继。
4. **B 站字段改名/删字段** → `mini_json` 解析要"缺字段不崩"（低概率但要有单测）。
5. **设备 TLS 1.1.0i 太旧被拒**（服务器要求 TLS1.3/新套件）→ 目前不会，但 1.1.0 已 EOL，属于长期风险。

### 6.8 中继放在哪：OpenWrt 路由器可行吗？

**结论：技术上能跑，但绝大多数 OpenWrt 路由器跑不动；只有 x86_64 软路由或 ARM64 + ≥1 GB RAM + USB 存储 + Docker 的机型才现实。**

**实测的硬门槛（OpenWrt 24.10 官方 packages feed，2026-10-22 查询）**：

| 目标 | 包总数 | 浏览器相关包（chromium/firefox/webkit/playwright/puppeteer） | Node 核心包 |
|---|---:|---|---|
| `x86_64` | 4,582 | **0 个** | `node_20.20.2`（ipk **13.5 MB**） |
| `aarch64_cortex-a53` | 4,561 | **0 个** | 有 |
| `mipsel_24kc` | 4,528 | **0 个** | **无** |

- 官方 feed **完全没有** Chromium/Firefox/WebKit/Playwright/Puppeteer 包 → 只能 Docker 或自己交叉编译（OpenWrt 默认 musl，Chromium 无官方 musl 构建，依赖地狱）。
- Docker 侧：`docker_27.3.1`（8.9 MB）+ `dockerd_27.3.1`（**21.4 MB**）+ `containerd` + `runc`，还要内核 cgroups/namespaces + ext4 U 盘做数据盘。
- Chromium 本体（容器内）约 **300–500 MB** 磁盘；内存经验值 **200–400 MB RSS**（浏览器 + 一个页面），**估算，未在 OpenWrt 上实测**。

**按机型分档**：

| 机型 | 跑无头浏览器 | 说明 |
|---|---|---|
| MT7621 / MT7628（MIPS，128–256 MB） | ❌ | 连 Node 都没有；内存差一个数量级 |
| ARMv7 512 MB（很多 WiFi6 路由） | ⚠️ 极勉强 | 需 USB + swap；Docker 内核支持常缺；易 OOM |
| ARM64 1 GB+（R4S/R5S 等） | ✅ 可行 | Alpine + Chromium，常驻 ~400 MB，需 USB SSD |
| x86_64 软路由（N100/J4125，4 GB+） | ✅ 很合适 | 与普通 Linux 无异；注意 CPU 周期性飙高影响转发 |

**如果一定要 OpenWrt 且不想上浏览器**：用 `node_20`（13.5 MB ipk）+ 平台签名 JS——
- 抖音：照 `f2` 的思路（固定串 MD5 + JS 算 `X-Bogus`，用假 `navigator`）；
- 快手：跑其 `__NS_hxfalcon` JS（**需要 `window`/`navigator`/`document` shim，未验证**）。
- 内存约 60–120 MB，CPU 每次几百 ms；但签名 JS 一更新就要跟着换，**"能跑"不等于"稳"**。

**推荐顺序**：① 已在运行的 PC/NAS（最省事）→ ② 树莓派 4/5（2 GB+）或旧 Android 手机（Termux）→ ③ x86 软路由 + Docker → ④ OpenWrt + Node（无浏览器，降级）→ ⑤ OpenWrt + 浏览器（仅 x86/ARM64 大内存）。

**其他坑**：
- Chromium 在容器里必须加 `--shm-size=256m`（默认 64 MB 的 `/dev/shm` 会让页面崩）。
- 无头浏览器默认带 `navigator.webdriver` 等自动化特征，**"能启动"≠"能拿到数据"**，可能还需要 stealth 处理。
- 建议**复用一个浏览器实例 + 多个 context/page**，不要每轮启动；路由器上启动一次要 3–10 s。
- 出口 IP 就是家里宽带 IP，与手机/PC 相同；**频率**才是关键，不是"换个设备就没事"。

---

## 7. 待用户确认（决策项）

1. **能否接受一台常开的局域网中继？**（PC / NAS / Home Assistant / 树莓派 / 小服务器）
   - 接受 → 抖音+快手都能做，设备侧工作量最小；
   - 不接受 → 只能做 B 站直连，抖音/快手一期不支持。
2. **能否提供平台 cookie / token？**
   - B 站：不需要；
   - 抖音：cookie 不能替代签名，只能略微降低风控概率；
   - 快手：cookie **完全无效**（签名是逐请求的）。
   - 若愿意提供，建议只放中继，设备不落地。
3. **需要哪些指标？**（在线人数 / 点赞 / 粉丝 / 标题 / 累计观看 / 开播状态 / 榜单）
   - 4 个房间 × 52×16，建议每房间最多 2 个数值 + 状态图标。
4. **房间号以什么形式给？**
   - B 站：房间号（短号/真号都行）；
   - 抖音：`live.douyin.com/<数字>` 里的数字（`web_rid`）；
   - 快手：`live.kuaishou.com/u/<principalId>` 里的字符串。
   - 是否接受"配置页粘贴分享链接、程序自动解析"？
5. **刷新频率与失败容忍？**（30 s / 60 s；失败时显示 `--` 还是保留旧值）
6. **是否允许设备直连公网？**（当前 WiFi 可出网，实测 ping 32 ms；若只允许中继，设备侧可以完全不出公网）
7. **快手是否一期就做？**（建议先不做，或标"实验性"）

---

## 8. 附录

### 8.1 证据索引

| 类型 | 内容 |
|---|---|
| 主机实测 | B 站：`get_info` / `getH5InfoByRoom` / `room_init` / `get_status_info_by_uids` / `getInfoByRoom(-352)` / 20 次限流探测 |
| 主机实测 | 抖音：`enter`（有/无 ttwid、无签名、限流前后）/ `reflow`（数字 room_id）/ 3 种 `web_rid` 映射尝试 / 明文 HTTP 301 |
| 主机实测 | 快手：SSR 页面 / `livedetail`（result:2）/ `websocketinfo` / `pcConfig`（result:1）/ 明文 HTTP 307 |
| 主机实测 | 设备 CA 包 × 三域名 TLS1.2 握手（`ssl_verify_result=0`） |
| 设备只读 | `/lib`、`/bin`、`/res/ui`、`/proc/meminfo`、`/proc/cpuinfo`、`ps`、`date`、`ping`、`adb pull`（`libssl` / `libcrypto` / `cacert.pem`） |
| 本地文件 | `build/device-backup/stock-libzkgui.so`（`libssl`/`libcrypto`/`cacert.pem`/`SSL_CTX` 字符串） |
| 外部资料 | 抖音签名实现 [f2 `webcast_signature.py`](https://raw.githubusercontent.com/Johnserf-Seed/f2/main/f2/apps/douyin/algorithm/webcast_signature.py)；快手签名 [__NS_sig3 解析](https://cloud.tencent.com.cn/developer/article/2435516)；抖音开放平台 [直播间详细数据](https://developer.open-douyin.com/docs/resource/zh-CN/mini-app/develop/server/operation/data-analysis/live-analysis/room-detail-analysis-data)（需资质） |

### 8.2 复现命令（脱敏，可直接跑）

```powershell
# B 站：一次拿全（标题/状态/人气/点赞/粉丝）
curl.exe -s -A "Mozilla/5.0" "https://api.live.bilibili.com/xlive/web-room/v1/index/getH5InfoByRoom?room_id=6"

# B 站：最稳兜底
curl.exe -s -A "Mozilla/5.0" "https://api.live.bilibili.com/room/v1/Room/get_info?room_id=6"

# B 站：按 UID 批量
curl.exe -s -A "Mozilla/5.0" "https://api.live.bilibili.com/room/v1/Room/get_status_info_by_uids?uids[]=50329118&uids[]=2"

# 抖音：先拿 ttwid（HEAD 即可，返回 404 但带 Set-Cookie）
curl.exe -s -I "https://live.douyin.com/"

# 抖音：网页房间数据（需要上面拿到的 ttwid）
curl.exe -s -H "Cookie: ttwid=<redacted>" -H "Referer: https://live.douyin.com/<web_rid>" `
  "https://live.douyin.com/webcast/room/web/enter/?aid=6383&app_name=douyin_web&live_id=1&device_platform=web&language=zh-CN&enter_from=web_live&cookie_enabled=true&web_rid=<web_rid>"

# 抖音：手机端点（要 19 位数字 room_id，无需 cookie）
curl.exe -s "https://webcast.amemv.com/webcast/room/reflow/info/?type_id=0&live_id=1&room_id=<room_id>&aid=1128&app_name=aweme&app_id=1128&device_platform=android"

# 快手：单房间（无签名，预期 result:2）
curl.exe -s -H "Referer: https://live.kuaishou.com/u/<principalId>" `
  "https://live.kuaishou.com/live_api/liveroom/livedetail?principalId=<principalId>"

# 设备侧只读检查
.\tools\platform-tools\adb.exe -s 192.168.31.124:5555 shell "ls -l /lib/libssl* /lib/libcrypto* /res/ui/cacert.pem; cat /proc/meminfo"
```

### 8.3 未验证 / 不确定项（明确标注）

| # | 项 | 状态 |
|---|---|---|
| 1 | 抖音 `reflow` 在房间**直播中**的 `user_count` 是否与网页 `user_count_str` 完全一致 | 只验证了已结束房间（`user_count=0`），字段存在且结构正确；未取到"直播中"的 `room_id` 做交叉比对（因为 `web_rid→room_id` 当时已被限流） |
| 2 | 抖音限流是"按 IP"还是"按 ttwid/UA" | 已排除 `ttwid` 因素：25 分钟后换**全新 `ttwid`** 单次请求仍为 0 字节 → 更像 **IP 级**；但未换网络/出口做对照 |
| 3 | 快手 `livedetail` 在有**有效签名**时是否返回 `watchingCount` | 未实现签名，无法验证；从首页 SSR 看字段名应为 `watchingCount` / `likeCount` |
| 4 | 快手是否有"官方允许"的公开接口 | 未找到；开放平台需资质 |
| 5 | 官方开放平台的具体申请门槛 | 只确认文档存在（抖音），未逐条阅读（页面为 JS 渲染，抓取为空） |
| 6 | 设备端 OpenSSL 1.1.0i 对上述域名的**真实**握手（非主机模拟） | 主机用同一 CA 包 + 强制 TLS1.2 验证通过；设备侧需真机跑一次才能 100% 确认（没有现成 CLI 可测） |
| 7 | `getH5InfoByRoom` 的长期稳定性 | 无官方文档，属网页内部接口 |

### 8.4 对现有代码库的影响面（只描述，不修改）

| 文件 | 影响 |
|---|---|
| `native/MultiTimer/src/net/` | 新增 `HttpClient.*`（TLS + HTTP/1.1 + chunked）、`live/LiveProvider.*`、`live/LivePoller.*` |
| `native/MultiTimer/src/config/ConfigStore.*` | 配置 schema 增加 `live` 段（房间列表、平台、指标选择、中继地址） |
| `native/MultiTimer/src/pages/` | 新增 `LivePage`（每页 4 行）+ `PageManager` 注册 |
| `native/MultiTimer/web-src/index.html` | 配置页增加直播间管理（粘贴链接/号码、指标勾选、中继地址） |
| 链接参数 | 若走 `dlopen`（方案 A），**不需要**改链接；若走方案 B 需要加 `-lssl -lcrypto` 和桩 |
| `/res` 体积 | 代码增量 ~50–100 KB（TLS 封装 + HTTP + live 模块），8 MiB 分区余量充足 |

> 本文只做调研，未改动上述任何文件。

---

## 9. 附：一句话给决策者

- **只想显示 B 站** → 设备直连可行，工作量中等（自写 TLS + HTTP），无需任何账号。
- **要显示抖音/快手** → 必须有一台常开中继；设备侧只做"HTTP 取 JSON + 显示"。
- **既不想加中继又要三家全支持** → 不可行（快手签名 + 抖音限流这两道坎过不去）。
