# TC002 桌面助手 / live-bridge

TC002 的桌面端工具，一个窗口内完成：

1. **直播数据转发**：在 PC 上运行 `bridge.py` 的核心逻辑，设备用明文 HTTP GET
   `/live?rooms=bilibili:6,douyin:...` 取统一 JSON；B 站/抖音/快手由 PC 代取。
2. **设备 setup**：局域网发现 TC002、状态总览、切页、**同步网络时间**、直播参数一键下发、
   WiFi 配网、原生编辑倒计时目标，以及**原生页面与显示配置**（页面顺序/类型/短标题、
   直播页房间与指标、时区/预警秒数/NTP 服务器/配色），全部在 exe 内完成，不调用设备网页。

- 协议来源：[`docs/V3-CONTRACT.md`](../../docs/V3-CONTRACT.md) §5（最高优先级）
- 部署/平台边界/防火墙/自测记录：[`docs/LIVE-BRIDGE.md`](../../docs/LIVE-BRIDGE.md)
- 不需要账号、不登录、不提交表单、不把 cookie 写盘

## 双击即用（Windows，推荐）

运行 [`dist/TC002Desk.exe`](dist/TC002Desk.exe)，无需安装 Python：

- **设备概览**：搜索/连接设备（默认 `:8080`），查看 WiFi/时间/当前页/直播健康，
  支持上一页/下一页、同步网络时间（默认国家授时中心 `ntp.ntsc.ac.cn`，失败回退其他 NTP / HTTP Date）、
  刷新直播、直播参数一键下发、WiFi 配网
- **直播转发**：默认监听 `0.0.0.0:8081`，显示本机局域网地址，可复制 `live.url` 与配置示例
- **倒计时目标**：直接读取/添加/删除/启用设备目标（日期 + 时间），保存写回设备
- **页面与显示**：原生编辑器（1–12 页；clock/clock_countdown/countdown/live/datetime；
  直播页 1–2 个房间，平台 + 房间号 + 标签 + 指标；默认页；上移/下移/删除），
  以及时区、预警秒数、NTP 服务器、五种界面颜色；保存时先读完整配置只改对应字段再写回
- **使用说明**：内置操作步骤、各平台房间号取法、快手/Edge 说明与常见问题

注意：

- 首次运行如弹出防火墙提示，请勾选“专用网络”并允许
- exe 已内置 Playwright 与 B 站/抖音/快手通道；快手优先调用系统 Edge，
  若系统没有 Edge，会回退到本机已安装的 Chromium
- 重新打包：先 `python -m pip install -r requirements-gui.txt`，再
  `powershell -ExecutionPolicy Bypass -File build_gui.ps1`

## 源码启动（Windows）

```powershell
cd D:\Project\ulanzi\tools\live-bridge
python -m pip install -r requirements-gui.txt
python forwarder_gui.py               # 单窗口桌面助手（转发 + 设备 setup + 原生配置）
python bridge.py                      # 只跑命令行中继，监听 0.0.0.0:8081
```

设备端也可手动配置（网页 `/api/config`）：

```json
"live": {"source":"hybrid","url":"http://<这台PC的IP>:8081/live","refreshSec":30,"timeoutMs":3000}
```

## 本机自测

```powershell
curl.exe "http://127.0.0.1:8081/live?rooms=bilibili:6"
curl.exe "http://127.0.0.1:8081/health"
python bridge.py --once --rooms bilibili:6            # 打印契约 JSON + 人读摘要
python -m unittest -v test_bridge                     # 40 个离线单测
```

## 支持边界（一句话版）

| 平台 | bridge 支持 | 条件 |
|---|---|---|
| B 站 | ✅ 完整（标题/状态/人气/点赞/粉丝） | 无 cookie；`getH5InfoByRoom` 失败自动降级 `get_info`。**按 2026-10-22 拍板，B 站优先由设备直连 HTTPS，这里保留作可选/兜底** |
| 抖音 | ✅ 匿名可用，但受 IP 风控 | 12 位 `web_rid` 走网页 `enter`（需匿名 `ttwid`）；19 位 `room_id` 走 `reflow`（无需 cookie）；`200`+0 字节 → `rate_limited` 并暂停 180s |
| 快手 | ⚠️ 必须装 Playwright | 未装 → `kuaishou_requires_browser`；装了用无头 Chromium 抓 `watchingCount/likeCount` |

> 只监听局域网；不要把 8081 映射到公网。平台接口随时可能变，失效时看
> `--log-level DEBUG` 与 `docs/LIVE-BRIDGE.md` 的错误码表。
