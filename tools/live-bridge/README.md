# live-bridge — MultiTimer v3 直播数据中继

TC002 设备只用**明文 HTTP GET** 取一份契约 JSON；本进程在 PC/NAS 上代为访问
B 站 / 抖音 / 快手，把结果统一成
`{"ok":true,"ts":...,"rooms":[{key,platform,room,ok,online,likes,fans,status,title,error}]}`。

- 协议来源：[`docs/V3-CONTRACT.md`](../../docs/V3-CONTRACT.md) §5（最高优先级）
- 部署/平台边界/防火墙/自测记录：[`docs/LIVE-BRIDGE.md`](../../docs/LIVE-BRIDGE.md)
- 依赖：Python 3.9+、`requests`；Playwright **可选**（只有快手需要）
- 不需要账号、不登录、不提交表单、不把 cookie 写盘

## 快速开始（Windows）

```powershell
cd D:\Project\ulanzi\tools\live-bridge
python -m pip install -r requirements.txt
python bridge.py                      # 监听 0.0.0.0:8081
```

设备端配置（网页 `/api/config`）：

```json
"live": {"source":"bridge","url":"http://<这台PC的IP>:8081/live","refreshSec":30,"timeoutMs":3000}
```

本机自测：

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
