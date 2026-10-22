> **GitHub 源码仓库说明**：本目录只包含源码仓库可再分发的 lib/libzkgui.so；update.img 含官方 stock /res，不在源码仓库中。

# MultiTimer 发布包（TC002 / Z21）

本目录是 `native/MultiTimer` 的构建产物，**不是**可以在官方固件 App 列表里
共存的原生插件。原因见 [`../native/MultiTimer/README.md`](../native/MultiTimer/README.md)
的“与官方固件的关系”一节。

> **当前状态（2026-10-22 22:02:00）**：本目录是 **v3.3** 发布包；
> `update.img`（21:49:28 生成）已通过官方 ADB 流程固化到设备 `/res` 并完成真机验证：
> `/tmp` 为空、`/data` 配置保留、回读 `/res/lib/libzkgui.so` = **722,532 字节** /
> `1A4959919B126AC35CE673BF14799A83CFE767063CA61A17896E882E07B8086E`（与
> `dist/lib/libzkgui.so` 一致）；`/res/lib/` 已带 OpenSSL 1.1.1w。
> 真机证据（NO COUNTDOWN / datetime 页 / B 站直连 / 网页自动地址 / 迁移）见
> [`../docs/STATE-2026-10-22-v3.md`](../docs/STATE-2026-10-22-v3.md) §10.2。

v3.3 = 在 v3.0（页面系统 + 直播监控）之上增加：

- **正计时 / NO COUNTDOWN**：分屏页/倒计时页在目标到达后从最近过期目标累计，颜色
  `colors.countup` 默认 `#00BFFF`；完全没有有效目标时显示 `NO COUNTDOWN`（dim），
  不再回时钟、不再显示日期；
- **datetime 页**：新增页面类型（时间 `HH:MM:SS` scale2 + 日期 `YYYY-MM-DD` scale1 dim）；
- **按键/IP**：中键**单击**显示全屏 IP（scale 2、无“IP”前缀、超宽水平滚动、12s）；
  **右键长按 3s** 进出 SoftAP；旋钮 CW/CCW 翻页不变；
- **网页地址自动**：不再保存 IP 地址列表，只用当前打开页面的 origin（自动补 `:8080`）；
  设备只保存 WiFi、IP 由 DHCP 获取；网页移除“分屏下半 blank”设置；
- **B 站设备直连已可用**：镜像 `/res/lib` 携带 OpenSSL 1.1.1w（Debian 11 armhf），
  `TlsClient` 优先加载；`hybrid` 下 B 站走 HTTPS 直连，抖音/快手走 bridge，
  B 站失败回退 bridge（60 分钟退避）；
- 保留 `push`、多 WiFi、NTP、80→8080 跳转。

## 1. 当前产物（v3.3）

| 文件 | 大小 | 用途 |
|---|---:|---|
| `lib/libzkgui.so` | **722,532** | FlyThings 应用主库（v3.3 最终产物） |
| `ui/main.ftu` | **162** | 空白主 Activity（52×16） |
| `EasyUI.cfg` | **399** | **`/tmp` 临时部署配置**：`startupLibPath=/tmp/lib/libzkgui.so`、`resPath=/tmp/ui/` |
| `update.img` | **1,942,076** | 已打包并已固化的 `/res` 镜像（Z21 平台码 `0x000606`，v3.3） |
| `update.img.sha256` | 77 | `update.img` 的 SHA256 校验文件 |
| `build-info.txt` | 1,628 | 输入/基线/中间产物/最终镜像的 hash、大小与命令（`generated_local=2026-10-22T21:49:28`） |

SHA256（v3.3）：

```text
lib/libzkgui.so  1A4959919B126AC35CE673BF14799A83CFE767063CA61A17896E882E07B8086E
ui/main.ftu      CFD891ABA792AB2B73681A78A5FA09D69BF7C5A54857EC038493DD3958A6D799
EasyUI.cfg       ECF97A876D7041C57E2E700735FEB4971868A529372E21268022692777821543
update.img       8B2BCF20A71202610FAC6CA9C2A3D339B8D457E2C48D358B7C791933114C16B4
```

- `update.img` = `res.tmp`（1,941,504 字节）+ 572 字节官方升级包头；payload 为
  1,941,504 字节（8 MiB `/res` 分区的 **23.15%**，v3.0 为 10.30%）。
- `build-info.txt` 记录
  `res_tmp_sha256=02A3B3FA5472AE99637037FB9A140382D9B0D197A3EAECB209E20FA1701EBBBF`、
  `stock_files=225`、`extra_files=2`、`staged_files=227`、stock 指纹
  `061F8B3C...19A5`、输入 `.so`/`main.ftu` 的 hash 和完整打包命令。
- `res.tmp` SHA256：`92E2209AB6FAFC9456737C858D3E26DB95B4BF17F0A271C543B9C2D5C5159160`。
- `update.img.sha256` 内容为 `8B2BCF20...C16B4  update.img`。

### 镜像新增内容：OpenSSL 1.1.1w（B 站直连）

v3.3 镜像在 stock `/res` 基础上**额外安装 2 个文件**到 `/res/lib/`：

| 文件 | 字节 | SHA256 |
|---|---:|---|
| `/res/lib/libssl.so.1.1` | 352,540 | `BE719492E3D2E1987C4CDFF191C929576B14E97B182D4328458E5820FE0B909D` |
| `/res/lib/libcrypto.so.1.1` | 1,737,128 | `5AFCFD7722883313678BADCC34828FC90C2F34ED9F26D0A68A8CC4062000FEBC` |

- 来源：Debian 11 (bullseye) armhf `libssl1.1_1.1.1w-0+deb11u1`；
  glibc 需求最高 `GLIBC_2.17`（设备 2.30，兼容）；许可为 OpenSSL/SSLeay（见
  [`../native/MultiTimer/third_party/openssl-armhf/README.md`](../native/MultiTimer/third_party/openssl-armhf/README.md)
  与 `LICENSE`）。
- 系统自带 `/lib/libssl.so.1.1` 是 OpenSSL 1.1.0i 的裁剪版（TLS client method 被编译掉，
  `SSL_connect` 141640BF），**不能用于 B 站直连**；`TlsClient` 优先加载镜像内的
  1.1.1w，系统库只作最后回退。
- 镜像内容见 `build-info.txt` 的 `extra_res`/`extra_tree_sha256`：
  `E17B59DE...9C5D`；staged 树 `75A28A35...98E4`。

### 历史版本

| 版本 | `lib/libzkgui.so` | `update.img` | 状态 |
|---|---|---|---|
| v3.2 | 722,532 B / `09753AAD...7DAF` | 1,942,076 B / `8C098B91...AD59` | 2026-10-22 21:20:51 固化，已被 v3.3 取代 |
| v3.0 | 718,436 B / `A4B1D7E8...B99F` | 864,828 B / `7058D4C0...7CAC` | 2026-10-22 20:13:55 固化，已被 v3.3 取代 |
| v2.1.2 | 480,876 B / `7A762407...B5DD9B` | 762,428 B / `A3C1655A...2FE9B8` | 2026-10-22 19:06:00 固化，已被取代 |
| v2.1.1 | `A9AE6B05...DAB55E` | `9378387A...59341` | 历史 |
| v2 | 435,820 B | 741,948 B | 历史 |

历史记录与 v3.2/v3.0/v2/v2.1 固化证据见
[`../docs/FLASH-RUNBOOK.md`](../docs/FLASH-RUNBOOK.md) §11–§14；
v2.1.2 冻结快照见
[`../docs/STATE-2026-10-22-v21.md`](../docs/STATE-2026-10-22-v21.md)。

### `dist/EasyUI.cfg` 与镜像内配置不是同一个文件

| 文件 | 路径语义 | 用途 |
|---|---|---|
| `dist/EasyUI.cfg`（399 字节） | `startupLibPath=/tmp/lib/libzkgui.so`、`resPath=/tmp/ui/` | 仅用于 `tools/deploy-tmp.ps1` 的 `/tmp` 临时部署 |
| 镜像内 `etc/EasyUI.cfg`（318 字节） | `startupLibPath=/res/lib/libzkgui.so`、`resPath=/res/ui/` | `/res` 固化镜像内**原样保留的 stock 配置**，不改成 `/tmp` |

镜像内 `EasyUI.cfg` 的 SHA256 仍是 stock 的
`E0C101F7E16DC8A1C8A6CAF9B73B47C393FAAF46C1F2DCE7D9FC8C8650215DCC`。
`make-image.ps1` 会校验它必须是 `/res` 路径且不能含 `/tmp/`。

## 2. 固化镜像内容

`update.img` 以 stock `/res` 为基线：**225 个文件、9,148,120 字节**；
v3.3 镜像共 **227 个文件**（stock 225 + `res-extra` 2）：

- 替换：
  - `lib/libzkgui.so` ← `dist/lib/libzkgui.so`
  - `ui/main.ftu` ← `dist/ui/main.ftu`
- 新增：
  - `lib/libssl.so.1.1`（OpenSSL 1.1.1w）
  - `lib/libcrypto.so.1.1`（OpenSSL 1.1.1w）

其余全部原样保留（`etc/EasyUI.cfg`、`lib/libAEC_LINUX.so`、`lib/libAPC_LINUX.so`、
`ui/` 下的官方资源、`bin/`、`font/`、`tr/` 等）。`build-info.txt` 记录
`staged_files=227`（stock 225 + extra 2）；`make-image.ps1` 会校验 staged 文件数
= stock + extra，并校验 `.so`/`main.ftu`/`EasyUI.cfg` 的 hash；uid/gid 1000、
mode `0770`。

它会**替换官方 `libzkgui.so`**，所以固化后开机直接进页面系统，
官方 App 列表/设置页不再存在；不能作为“官方固件里的一个图标”与官方 App 共存。

## 3. 官方 ADB 固化（v3.3 已执行并验证）

官方流程（`tools/flash-update.ps1` 内部执行）：

```powershell
$adb = "..\tools\platform-tools\adb.exe"
& $adb push update.img /tmp/update.img
& $adb shell "setprop sys.zkupgrade.flag 255"
& $adb shell "setprop sys.zkupgrade.dir /tmp"
& $adb shell "setprop ctl.restart zkswe"
```

推荐用脚本，默认 DryRun，只有 `-Confirm` 才写设备：

```powershell
# 只读预检
powershell -NoProfile -ExecutionPolicy Bypass -File ..\tools\flash-update.ps1 `
  -Image dist\update.img

# 人工复核后执行
powershell -NoProfile -ExecutionPolicy Bypass -File ..\tools\flash-update.ps1 `
  -Image dist\update.img -Confirm
```

2026-10-22 v3.3 的执行结果（真机 `192.168.31.124`，Z21，官方固件 1.1.3 / MCU V1.0.17）：

- 镜像生成时间 `2026-10-22T21:49:28`；设备按官方升级流程重启后恢复；
- `/tmp` 为空；
- `/res/lib/libzkgui.so` = **722,532 字节**，回读 SHA256 `1A495991...8086E`；
- `/res/lib/libssl.so.1.1` + `libcrypto.so.1.1` 已随镜像安装（OpenSSL 1.1.1w）；
- `/res/etc/EasyUI.cfg` 仍为 **318 字节** stock `/res` 配置；
- `/data/multitimer.conf`、`/data/multitimer.wifi`、`/data/preferences.json` **保留**，
  配置保持 v3（v2→v3 迁移已在 v3.0 完成；`colors.countup` 缺省补默认）；
- WiFi 连接保留（SSID 不记录在仓库）；
- `GET /api/status`、`GET /api/config`、`/api/page`、`GET /api/live/status` 正常；
  80 端口 302 到 `:8080`；
- 真机行为：**NO COUNTDOWN**（无目标 p1 `text2` / p2 `text`）、**datetime 页**
  （`text=22:01:27`、`text2=2026-10-22`）、**B 站设备直连**（停止 bridge 后示例房间
  仍 `state=ok`）、网页 v3.3 自动地址加载；
  详见 [`../docs/STATE-2026-10-22-v3.md`](../docs/STATE-2026-10-22-v3.md) §10.2。

需要复核时按 [`../docs/FLASH-RUNBOOK.md`](../docs/FLASH-RUNBOOK.md) §13.6 的只读命令检查。
完整命令、证据、风险与回滚见
[`../docs/FLASH-RUNBOOK.md`](../docs/FLASH-RUNBOOK.md)；
v3.3 当前快照见
[`../docs/STATE-2026-10-22-v3.md`](../docs/STATE-2026-10-22-v3.md)；
v2.1.2 冻结快照见
[`../docs/STATE-2026-10-22-v21.md`](../docs/STATE-2026-10-22-v21.md)。

### 再次升级（v3.3 之后）

已固化设备更新版本时，用新的 `libzkgui.so` 重新执行 `make-image.ps1` 生成
`update.img`（默认会带上 `res-extra` 的 OpenSSL 库），再走官方 ADB/TF 卡流程；
`deploy-tmp.ps1` 只用于开发期 `/tmp` 覆盖调试，不能替代 `/res` 升级。
**注意**：`/tmp` 部署不带 `res-extra`，因此 B 站直连只有在 `/res` 固化后才可用。

## 4. 临时运行（`/tmp`，不固化）

设备与电脑同一局域网，ADB 可连（TC002 Wi-Fi ADB，默认 5555）：

```powershell
powershell -ExecutionPolicy Bypass -File ..\tools\deploy-tmp.ps1 -DeviceIp 192.168.31.124
```

脚本做的是 FlyThings IDE “下载调试”的等价流程：把 `dist/EasyUI.cfg`、
`lib/libzkgui.so`、`ui/main.ftu` 推送到 `/tmp/{EasyUI.cfg,lib,ui}`，
`setprop ctl.restart zkswe`。
**不会写 `/res`，断电/重启即恢复 `/res` 中的版本**；也不会安装 OpenSSL 库
（`/tmp` 下 B 站直连会回退系统 1.1.0i 并失败，属预期）。

恢复：

```powershell
powershell -ExecutionPolicy Bypass -File ..\tools\deploy-tmp.ps1 -DeviceIp 192.168.31.124 -Restore
```

> 在已固化 v3.3 的设备上，`-Restore` 只删除 `/tmp` 覆盖层并重启，**不会恢复官方固件**；
> 恢复官方固件只能用 USB-C 旁复位键（会清空 `/data`）。
>
> ⚠️ 不要用 `adb push` 直接覆盖正在运行的 `/tmp/lib/libzkgui.so`；运行中的 zkgui
> 已 mmap 该文件，覆盖会让进程崩溃。`deploy-tmp.ps1` 已改成 `.new` + `rm` + `mv`。

然后手机访问 `http://<设备IP>:8080/` 或 `http://<设备IP>/`（80 会 302 到 8080）；
设备 IP 由 DHCP 获取，中键单击可在 LED 上查看。

## 5. 回滚

如果升级后黑屏/无法启动：

**按住 USB-C 旁的复位键约 5 秒再上电**，设备会刷回官方固件，同时**清空 `/data`**
（WiFi 凭据、`multitimer.conf`、时钟状态都会丢）。恢复的官方 App 版本可能比原来的
1.1.3 更旧；恢复后需重新配网、重新刷入本工程。

- **不要**把 `../build/device-backup/res-stock.img` 当 `update.img` 刷入：它是 8 MiB
  原始分区快照，没有升级包头，官方升级器不接受。
- **不要** `adb push` 到 `/data` 或直接写 `/dev/block/mtdblock3`；没有 root/remount，
  且可能卡死 USB。
- **v3 配置不能回滚给 v2 固件使用**：回滚到 v2 固件时，v2 会因 `version!=2`
  按默认配置运行；回滚前请先 `adb pull /data/multitimer.conf` 备份配置。
  v3.3 与 v3.2/v3.0 配置兼容（`colors.countup` 可缺省，datetime 页仅在 v3.3+ 有效；
  回滚旧固件会忽略新增类型/字段）。
- 当前没有“保留 `/data` 回滚到官方 `/res`”的可靠手段；复位键是既定恢复路径。

## 6. 备份

- 官方 `update.img` 备份：`../build/device-backup/_mnt_storage_update.img`
  （候选备份，未真机验证）
- 真机 `/res` 分区备份：`../build/device-backup/res-stock.img`（8 MiB 块设备）
- 真机 `/config` 备份：`../build/device-backup/config-stock.img`
- 真机原始 `setting.ini` / `wpa_supplicant.conf` / `zkgui` / `init.rc`：
  `../build/device-backup/`
- 固化后回读的 `/res` 关键文件：`../build/verify/`
  （v3.3 回读 `flashed-v3g.so`，722,532 B / `1A495991...`，22:01）

`build/` 目录是开发/测试产物，可按需删除；删掉后回滚只能靠复位键。
`build/device-backup/_data_setting.ini` 与
`_data_misc_wifi_wpa_supplicant.conf` 可能含敏感凭据，**不要提交、不要外传**；
仓库与文档不记录真实 WiFi 密码、cookie 或 token。
