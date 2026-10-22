# TC002 MultiTimer v2 / v2.1 / v3 /res 固化 Runbook

> 当前状态：**v3.3 已于 2026-10-22 21:49:28 生成镜像并完成 `/res` 固化与真机验证**：
> `/res` 回读 `libzkgui.so` **722,532 字节**，SHA256 `1A4959919B126AC35CE673BF14799A83CFE767063CA61A17896E882E07B8086E`；
> `dist/update.img` **1,942,076 字节**，SHA256 `8B2BCF20A71202610FAC6CA9C2A3D339B8D457E2C48D358B7C791933114C16B4`。
> 设备：`192.168.31.124:5555`（WiFi ADB），`ro.product.model = Zkswe_SSD21X_SPINOR`（Z21）。
> 当前运行方式：应用已写入 `/res`，stock `/res` 已被本镜像替换；`/data` 未被本流程清空；
> `/res/lib/` 额外含 OpenSSL 1.1.1w（`libssl.so.1.1` + `libcrypto.so.1.1`）。
> `tools/flash-update.ps1` 默认 DryRun；真正写设备必须显式加 `-Confirm`。
>
> **历史与当前的关系**：本文 §1–§6、§9.1、§9.3 记录的是 **v2 首次固化**
> （2026-10-22 17:08:00，435,820 字节 / 741,948 字节镜像），保留作为历史；
> **§11 = v2.1.2（历史）、§12 = v3.0（历史）、§13 = v3.2（历史）；
> 当前设备与产物以 §14（v3.3）为准**。
> v2.1.1（19:00:53，`A9AE6B05...` / `9378387A...`）也已在 §11.1 中作为历史列出。
> 各版使用同一套命令、同一 stock 基线（225 个文件）；v3.2 起额外叠加
> `native/MultiTimer/res-extra` 的 2 个 OpenSSL 库（staged 227 个文件）。

---

## 0. 红线

1. **禁止 `adb push` 到 `/data`**。`/data` 是 JFFS2，直接 push 可能卡死 USB；所有临时文件只放 `/tmp`。
2. **禁止直接写 `/dev/block/mtdblock*`、`/res`**。没有 root/remount 流程，也不要用 `dd` 尝试恢复 `res-stock.img`。
3. 本镜像替换的是整个 `/res` 分区中的 `zkgui`，**不是官方 App 的一个插件**；刷入后官方 App 列表/设置页不再存在。
4. 默认 DryRun；只有人工复核后显式 `-Confirm` 才允许写设备。
5. 长按复位键恢复官方固件会**清空 `/data`**（WiFi 凭据、`multitimer.conf`、时钟状态都会丢失）。
6. v2.1 的 SoftAP 配网窗口（15 分钟、固定密码）是**设备本地物理操作**，不通过 ADB 开关；
   刷机流程不要尝试用 HTTP 接口打开 AP。

---

## 1. 固化前只读检查结论（2026-10-22）

### 1.1 设备与分区

| 项目 | 值 |
|---|---|
| `ro.product.model` | `Zkswe_SSD21X_SPINOR` |
| `ro.product.device` | `swaio` |
| `ro.build.version.release` | `flythingsV2.1` |
| `init.svc.zkswe` / `sys.zkapp.state` | `running` / `running` |
| 官方固件版本 | 任务给定 **1.1.3**；`getprop ro.app.version` 为空，未能从属性独立复核 |
| MCU 版本 | **V1.0.17** |
| `/data` | 8 MiB（mtd6），已用 332 KB，可用 7,860 KB |
| `/tmp` | tmpfs 16,504 KB，已用约 400 KB，可用 16,104 KB |
| `/res` | mtd3，8 MiB，squashfs(xz)；挂载为只读，固化前已用约 2.8 MiB |

`/proc/mtd`：

```text
mtd0 BOOT0    0x00050000
mtd1 KERNEL   0x001f0000
mtd2 rootfs   0x00450000
mtd3 res      0x00800000   <- 本镜像目标，8 MiB
mtd4 config   0x000b0000
mtd5 MISC     0x00040000
mtd6 data     0x00800000   <- 不触碰
mtd7 UDISK    0x00880000
```

### 1.2 stock `/res` 清单

`adb pull /res build/flash/res-stock` 拉取结果：**225 个文件，9,148,120 字节**，13 个目录（含根目录），
并与 `build/device-backup/res-stock.img`（8 MiB 原始分区快照）用 7-Zip 解包后逐文件 SHA256 比对：
**225/225 一致，无缺失、无多余、hash 全同**。完整校验清单见 `build/flash/res-stock.sha256`（225 行）。

| 目录 | 文件数 | 字节数 | 内容 |
|---|---:|---:|---|
| `bin/` | 4 | 871,000 | `gattserverbin`、`hciattach`、`hciconfig`、`hcitool` |
| `etc/` | 1 | 318 | `EasyUI.cfg` |
| `font/` | 0 | 0 | 空目录，保留 |
| `lib/` | 3 | 7,665,360 | `libAEC_LINUX.so`、`libAPC_LINUX.so`、`libzkgui.so` |
| `tr/` | 0 | 0 | 空目录，保留 |
| `ui/` | 8 | 232,872 | `main.ftu`、`test.ftu`、默认图标、`cacert.pem`、`ulanzi_logo.png` |
| `ui/app_icons/` | 37 | 13,926 | 官方图标 |
| `ui/audio/` | 8 | 69,008 | 官方提示音 |
| `ui/font_image/` | 146 | 17,524 | 数字/字母点阵图 |
| `ui/web/` | 11 | 247,466 | 官方网页 |
| `ui/web/i18n/` | 3 | 27,336 | 官方网页翻译 |
| `ui/wifiStatus/` | 4 | 3,310 | WiFi 状态图 |

stock 关键 hash（固化前）：

```text
lib/libzkgui.so    F9CBE0704606FCFB549847E211154F4C56A0AD8BA01308EB065DE3C0B5FD5ABA
etc/EasyUI.cfg     E0C101F7E16DC8A1C8A6CAF9B73B47C393FAAF46C1F2DCE7D9FC8C8650215DCC
ui/main.ftu        DC163A70EABA1645A639E78692813638748052F45A389E66D0ACE9C3A8B73F56
ui/test.ftu        CFD891ABA792AB2B73681A78A5FA09D69BF7C5A54857EC038493DD3958A6D799
```

stock 目录整体指纹（路径 + 大小 + SHA256 的排序拼接后再 SHA256）：

```text
061F8B3CDEB88827CD83CEC20419DA02D56CD757E180E8AC01785466D36119A5
```

### 1.3 stock `EasyUI.cfg`

原版文件**已经是 `/res` 路径**，本流程原样保留，不改成 `/tmp`：

```json
{
	"baud":"115200",
	"rotateTouch":0,
	"rotateScreen":0,
	"startupLibPath":"/res/lib/libzkgui.so",
	"languageCode":"zh_CN",
	"defBrightness":-1,
	"screensaverTimeOut":-1,
	"touchDev":"/dev/input/event0",
	"languagePath":"/res/tr/",
	"uart":"ttyS1",
	"startupTouchCalib":false,
	"zkdebug":false,
	"resPath":"/res/ui/"
}
```

`tools/make-image.ps1` 会校验：

- `startupLibPath` 必须是 `/res/lib/libzkgui.so`；
- `resPath`、`languagePath` 必须以 `/res/` 开头；
- 文件里不能出现 `/tmp/`。

### 1.4 固化后状态（实际回读）

| 项目 | 固化后 |
|---|---|
| `/tmp` | **为空**（升级包已被升级流程消费/清理） |
| `/res/lib/libzkgui.so` | **435,820 字节**，SHA256 `123E18480E5825EB3992AE8A4C7319EA9ACCC0BB84144CA174061A71323F3DEC` |
| `/res/etc/EasyUI.cfg` | **318 字节**，SHA256 `E0C101F7E16DC8A1C8A6CAF9B73B47C393FAAF46C1F2DCE7D9FC8C8650215DCC`，仍为 `/res` 路径 |
| `/res/ui/main.ftu` | **162 字节**，SHA256 `CFD891ABA792AB2B73681A78A5FA09D69BF7C5A54857EC038493DD3958A6D799` |
| `/data/multitimer.conf` | **保留** |
| `/data/multitimer.wifi` | **保留** |
| `/data/preferences.json` | **保留** |
| 服务 | `init.svc.zkswe = running`、`sys.zkapp.state = running` |
| HTTP | `http://192.168.31.124:8080/api/status` 返回 **200** |

固化后从设备回读的 `/res` 关键文件已保存在 `build/verify/`（`EasyUI.cfg`、`libzkgui.so`、`main.ftu`），
hash 与上表一致，可作为本次固化的本地证据。

---

## 2. 备份清单

目录：`build/device-backup/`。`build/` 已被 `.gitignore` 忽略；仍建议把整个目录另存到仓库外的离线介质。

| 文件 | 字节 | SHA256 | 用途 |
|---|---:|---|---|
| `res-stock.img` | 8,388,608 | `63D35BFD...0E91E1E2` | stock `/res` 原始分区快照，**仅存档，不能当 update.img 刷** |
| `config-stock.img` | 720,896 | `0CBE147E...5383BE` | stock `/config` 分区快照 |
| `stock-libzkgui.so` | 7,488,620 | `F9CBE070...5FD5ABA` | stock 启动库 |
| `_mnt_storage_update.img` | 2,781,756 | `708C7844...11AB99EE` | 官方 `update.img` 候选备份（头部 `ZKSWEV1.0`，未真机验证） |
| `_res_etc_EasyUI.cfg` | 318 | `E0C101F7...50215DCC` | stock `/res/etc/EasyUI.cfg` |
| `_res_ui_main.ftu` | 452 | `DC163A70...8B73F56` | stock `main.ftu` |
| `_res_ui_test.ftu` | 162 | `CFD891AB...8A6D799` | stock `test.ftu` |
| `_mnt_storage_zkupgradetipbin` | 9,600 | `9ADA423E...44E37A0` | 官方升级提示素材 |
| `_data_setting.ini` | 626 | 不记录/不外传 | 官方 App 设置，**可能含 WiFi/云端 token** |
| `_data_misc_wifi_wpa_supplicant.conf` | 90 | 不记录/不外传 | WiFi 配置，**可能含明文 psk** |
| `_bin_zkgui` / `_bin_zkdaemon` / `_bin_zkdisplay` | 9,564 / 13,692 / 26,064 | — | stock `/bin` 可执行文件 |
| `_config_board.ini` / `_etc_init.rc` | 472 / 3,001 | — | stock 板级/init 备份 |

注意：

- `_data_setting.ini` 和 `_data_misc_wifi_wpa_supplicant.conf` 含敏感凭据，**不要提交、不要贴到聊天/issue**。
- `res-stock.img` 是**原始分区快照**，没有 572 字节升级包头部，不是官方 `update.img`。官方 ADB/TF 卡升级不接受它；不要把它改名为 `update.img` 去刷。
- 真正可用的救砖路径是复位键；备份主要用于比对、审计和极端情况下的离线分析。
- `/data` 没有完整镜像备份；最坏情况下复位会清空它。

---

## 3. 最终镜像布局

### 3.1 内容

镜像根目录 = stock `/res` 的完整副本（**225 个文件**），只替换两个文件：

```text
/
├── bin/                      stock 4 个可执行文件（原样）
├── etc/
│   └── EasyUI.cfg            stock 原样（318 字节，/res/lib、/res/ui、/res/tr）
├── font/                     stock 空目录
├── lib/
│   ├── libAEC_LINUX.so       stock 原样
│   ├── libAPC_LINUX.so       stock 原样
│   └── libzkgui.so           ← 替换为 -SoPath
├── tr/                       stock 空目录
└── ui/
    ├── main.ftu              ← 替换为 -MainFtu
    ├── test.ftu              stock 原样
    ├── app_icons/            stock 原样
    ├── audio/                stock 原样
    ├── font_image/           stock 原样
    ├── web/                  stock 原样
    ├── wifiStatus/           stock 原样
    ├── cacert.pem            stock 原样
    └── ...
```

> `dist/EasyUI.cfg`（399 字节）是 **`/tmp` 临时部署配置**，`startupLibPath` 指向
> `/tmp/lib/libzkgui.so`、`resPath` 指向 `/tmp/ui/`，**不会**进入本镜像。
> 镜像内使用的是 stock `/res/etc/EasyUI.cfg`（318 字节，`/res` 路径，原样保留）。

### 3.2 打包参数

```text
zkswe_mkimg.exe imgout res.tmp -noappend -force-uid 1000 -force-gid 1000 -comp xz
fsimg.exe -i res:res.tmp -o update.img -p 0x000606
```

- `res.tmp`：SquashFS 4.0，XZ，块 128 KiB；所有文件 uid/gid = 1000，目录/文件 mode 为 `0770`（与 stock 一致，7-Zip 已核对）。
- `update.img`：官方 Z21 升级包，平台 `0x000606`；实测包装头为 **572 字节**，payload 就是 `res.tmp`。
- **硬上限**：`res.tmp <= 8,388,608` 字节（8 MiB，mtd3）。`update.img = res.tmp + 572`，总大小可以比 8 MiB 多 572 字节，但 payload 不能超。

### 3.3 v2 最终镜像（历史，已被 v2.1 取代）

| 文件 | 字节 | SHA256 |
|---|---:|---|
| `dist/update.img` | **741,948** | `03DCA727595C993DE4BA665E18440FFF61E08CE1875291660CB3BDC2FFE1AAC7` |
| `build/flash/image-work/res.tmp` | **741,376** | `4285FBAB3613F1D5AF7AF6E7E60BE7DA19CA9229BCAEC5617387646B817821F5` |
| `dist/lib/libzkgui.so` | **435,820** | `123E18480E5825EB3992AE8A4C7319EA9ACCC0BB84144CA174061A71323F3DEC` |
| `dist/ui/main.ftu` | **162** | `CFD891ABA792AB2B73681A78A5FA09D69BF7C5A54857EC038493DD3958A6D799` |

- `res.tmp` 占 8 MiB 分区的 **8.84%**，余量充足；`update.img` 比 payload 多 572 字节包头。
- `dist/update.img.sha256` 内容为 `03DCA727...1AAC7  update.img`。
- `dist/build-info.txt` 锁定输入 `.so`/`main.ftu`/stock 指纹/`res.tmp`/`update.img` 的大小与 SHA256、平台码和完整命令。
- 验证：7-Zip 解包后 **225/225 文件**；除 `lib/libzkgui.so` 与 `ui/main.ftu` 外，全部文件 hash 与 stock 基线相同；
  `EasyUI.cfg` 仍为 `/res` 路径；uid/gid 1000，mode `0770`。

> 早期演练镜像只用于预演，已被最终 741,948 字节镜像取代；
> 不要再拿 `build/flash/dryrun/update.img` 或任何旧的最小镜像刷设备。

---

## 4. 生成正式镜像

### 4.1 前置

- `build/flash/res-stock` 存在且 225 文件；
- `-SoPath` 指向已通过 `/tmp` 真机验证的 `libzkgui.so`；
- `-MainFtu` 存在；
- `build/ide-tools/{zkswe_mkimg.exe,fsimg.exe}` 及其 DLL 存在；
- 当前无其他脚本在写同一 `WorkDir`。

### 4.2 先 DryRun

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\make-image.ps1 -DryRun
```

DryRun 会：

- 检查 stock 完整性、`EasyUI.cfg` 路径、ELF 头（ARM 32-bit little-endian）；
- staging 到 `build/flash/image-work-dryrun` 并逐文件核对，**不删除**正式打包的 `image-work`；
- 打印将执行的 `zkswe_mkimg` / `fsimg` 命令；
- **不调用打包工具、不创建 OutDir、不写 update.img**。

### 4.3 正式打包

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\make-image.ps1 `
  -SoPath dist\lib\libzkgui.so `
  -MainFtu native\MultiTimer\ui\main.ftu `
  -OutDir dist
```

输出：

```text
dist/update.img
dist/update.img.sha256
dist/build-info.txt
build/flash/image-work/res.tmp
build/flash/image-work/zkswe_mkimg.log
build/flash/image-work/fsimg.log
```

> `make-image.ps1` 的默认 `-SoPath` 只是占位（`build/ide-workspace/...`），不同 workspace 可能留下
> 不同版本的 `.so`。**正式打包必须显式传 `-SoPath dist\lib\libzkgui.so`**，
> 并以 `dist/build-info.txt` 记录的 hash 为准，不要混用 build 目录里的旧产物。

本次正式镜像生成时间：**2026-10-22 17:07:48**（`dist/build-info.txt` 的 `generated_local`）。

---

## 5. 刷写（v2 历史：官方 ADB 固化，已执行）

### 5.1 先跑默认 DryRun

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 `
  -Image dist\update.img
```

不传 `-Confirm` 时，脚本只做：

- 本地镜像检查：存在、payload ≥512 KiB（挡住只含少量文件的最小镜像）、`ZKSWEV1.0` 头、payload ≤8 MiB；
- 备份检查：`res-stock.img`、`stock-libzkgui.so`、`_mnt_storage_update.img`、`_res_etc_EasyUI.cfg`、两个 `/data` 配置备份；
- 设备检查：ADB 5555、型号 `Zkswe_SSD21X_SPINOR`、`/tmp` 可用空间 ≥ 镜像 + 2 MiB；
- 打印 4 条将执行的命令、验证命令和回滚说明；
- **不 push、不 setprop、不重启。**

### 5.2 实际执行

2026-10-22 17:08:00 左右，在以下条件满足后执行：

- DryRun 全部通过；
- 正式 `update.img` 的 SHA256 已记录（`03DCA727...1AAC7`）；
- 已在 `/tmp` 真机验证过同一 `.so`；
- 已接受“官方 App 被替换”和“最坏情况复位清空 `/data`”；
- 设备供电稳定、物理上能够按到复位键。

执行命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 `
  -Image dist\update.img -Confirm
```

`-Confirm` 是**本脚本自定义的写设备开关**，不是 PowerShell 的通用确认提示。

脚本执行的官方流程：

```bash
adb push ./update.img /tmp/update.img
adb shell setprop sys.zkupgrade.flag 255
adb shell setprop sys.zkupgrade.dir /tmp
adb shell setprop ctl.restart zkswe
```

然后等待：

1. ADB 断开（设备重启/升级）；
2. ADB 恢复；
3. 读取 `/res` 关键文件与 `EasyUI.cfg`；
4. 轮询 `http://<ip>:8080/api/status`（除非 `-SkipHttpCheck`）。

本次执行结果：ADB 正常断开并恢复，`init.svc.zkswe = running`、
`sys.zkapp.state = running`，`/res` 关键文件更新，HTTP 返回 200。

### 5.3 `adb push` stderr 误判修复

`tools/flash-update.ps1` 的 `Invoke-Adb` 早期在 PowerShell 5.1 下会把 `adb push`
写入 stderr 的进度输出当成终止错误（脚本设置了 `$ErrorActionPreference='Stop'`），
导致 **push 成功但流程仍抛异常**。现已修复：

- 调用 adb 期间临时把 `$ErrorActionPreference` 设为 `Continue`；
- 只以 `$LASTEXITCODE` 判定成败；
- 调用结束后恢复原设置。

本次固化使用的就是修复后的脚本，`adb push` 成功且后续 setprop/重启均正常执行。

### 5.4 注意事项

- 刷写期间**绝对不能断电**。
- 设备重启后 IP 可能变化（DHCP）；ADB 恢复不了时先查路由器/屏幕 IP，再 `adb connect <new-ip>:5555`。
- `/tmp` 只有约 16 MB；当前 0.71 MiB 镜像足够，但不要把大文件继续塞进 `/tmp`。
- 本流程不会写 `/data`；正常升级后 WiFi 与 `multitimer.conf` 应保留。复位键是例外，它会清空 `/data`。

---

## 6. 刷后验证（v2 历史证据）

> 本节是 2026-10-22 17:08:00 首次固化 v2 的证据，保留用于对照。
> v2.1 的刷后验证见 §11.3。

```powershell
$adb = ".\tools\platform-tools\adb.exe"
$ip  = "192.168.31.124"

# 1. App HTTP 状态：应返回 200 和 JSON（mode 可为 clock/countdown/invalid）
Invoke-WebRequest -UseBasicParsing "http://${ip}:8080/api/status" |
  Select-Object -ExpandProperty Content

# 2. /res 关键文件与 EasyUI.cfg
& $adb -s "${ip}:5555" shell "ls -l /res/etc/EasyUI.cfg /res/lib/libzkgui.so /res/ui/main.ftu"
& $adb -s "${ip}:5555" shell "cat /res/etc/EasyUI.cfg"

# 3. 服务与防砖标志
& $adb -s "${ip}:5555" shell "getprop init.svc.zkswe; getprop sys.zkapp.state; getprop sys.zkupgrade.flag"

# 4. /tmp 与 /data
& $adb -s "${ip}:5555" shell "ls -l /tmp"
& $adb -s "${ip}:5555" shell "ls -l /data/multitimer.conf /data/multitimer.wifi /data/preferences.json"

# 5. 日志
& $adb -s "${ip}:5555" logcat -d -v time
```

### 6.1 判定标准与实测

| 检查项 | 标准 | 实测 |
|---|---|---|
| `/tmp` | 升级包不再残留 | **为空** |
| `/res/lib/libzkgui.so` | 等于本次输入 `.so` | **435,820 字节**，SHA256 `123E1848...3DEC` |
| `/res/etc/EasyUI.cfg` | 仍为 stock 318 字节 / `/res` 路径 | **318 字节**，SHA256 `E0C101F7...15DCC` |
| `/res/ui/main.ftu` | 等于本次输入 | **162 字节**，SHA256 `CFD891AB...6D799` |
| `/data` | 配置/WiFi/时钟状态保留 | `multitimer.conf`、`multitimer.wifi`、`preferences.json` 均在 |
| 服务 | 升级后正常运行 | `init.svc.zkswe = running`、`sys.zkapp.state = running` |
| HTTP | `/api/status` 200 | **200** |
| 冷启动 | 恢复时钟 + SNTP | 见 6.2 |
| 功能 | 倒计时/对时/时区/显示 | 见 6.3 |

### 6.2 冷启动日志关键行（原样）

```text
TimeKeeper: restored system clock from flash epoch=...
WifiBootstrap: SNTP sync ok
mainLogic: loaded /data/multitimer.conf (0 targets)
```

说明：

- 无 RTC，冷启动先从 flash 恢复上次保存的 epoch；实测恢复值比真实时间**落后 43 秒**，
  WiFi 自动连上后 SNTP 校正；
- 本次冷启动使用空目标配置，因此 `loaded ... (0 targets)`，屏幕进入时钟模式；
  目标配置的重启保持已另行验证（见 6.3）。

### 6.3 固化后功能验证

- **双目标顺序切换**：90 秒 / 180 秒两个目标，预警 7 秒变红；第一个目标结束后
  自动切第二个；
- **严格绑定时钟**：时钟回拨 1 小时后，剩余秒数立即从 75 变为 3585；
- **时区分钟偏移**：`UTC+08:00` → `UTC+05:30` 后剩余秒数 +9000；
- **远目标格式**：剩余 ≥100 小时显示 `4d04:02:02`，小号字；
- **配置持久化**：保存后重启，目标/时区/颜色/预警仍保持；
- **网页**：390×1200 视口无横向溢出，0 console error，0 exception；
  主机单测 92/92、网页自检 55/55（v2 当时），FlyThings 构建 0 errors / 0 warnings。

HTTP 不通但 ADB 正常时：先收集 `logcat -d -v time`，不要立刻重复刷机。

---

## 7. 回滚

### 7.1 首选：复位键

**按住 USB-C 旁的复位键，再上电/保持按住**，等待设备恢复官方固件（官方 SDK `README_CN.md` §5 的写法）。
社区资料与任务约定进一步给出“约 5 秒”；本仓库按这个更保守的时序执行。

- 恢复官方固件，**同时清空 `/data`**（约 5 秒/清空 `/data` 来自社区 install-tools 与 `docs/RESEARCH-tc002-wifi-deploy.md` §4.5，官方 README 未写这两点）；
- WiFi 凭据、`multitimer.conf`、时钟状态全部丢失；
- 恢复的官方 App 版本可能比设备原来的 1.1.3 更旧（社区资料明确提示）；
- 恢复后需重新用官方 App 配网。

### 7.2 不推荐的路径

- **不要**把 `build/device-backup/res-stock.img` 当 `update.img` 刷入：它是 8 MiB 原始分区快照，没有升级包头，官方升级器不接受。
- **不要**尝试 `adb push` 到 `/data` 或直接写 `/dev/block/mtdblock3`；没有 root/remount，且可能卡死 USB。
- `tools/deploy-tmp.ps1 -Restore` 只清理 `/tmp` 部署并重启；**一旦 `/res` 已被替换，它不能恢复 stock `/res`**。
- 当前没有“不丢 `/data` 回滚到官方 `/res`”的可靠手段；复位键是既定恢复路径。

### 7.3 TF 卡候选

若设备仍能进入官方升级界面：把 `_mnt_storage_update.img` 重命名为 `update.img` 放到 **FAT32** TF 卡根目录，上电按官方升级界面操作。该文件只是候选备份，未在真机验证，版本/完整性未知；不要把它当作确定性回滚手段。

---

## 8. 风险与已知限制

1. **替换整个 `/res`，不是插件**：刷入后官方 App、官方设置页、官方网页入口都被替换；无法与官方 App 共存。
2. **复位清空 `/data`**：这是官方恢复路径的既定行为；没有完整的 `/data` 可刷回镜像。
3. **原始分区备份不能直接恢复**：`res-stock.img` 仅用于比对/存档；真正的救砖是复位键。
4. **8 MiB 硬上限**：`res.tmp` 不能超过 8 MiB；当前 v2.1.2 为 761,856 字节（9.08%），余量充足，但以后新增资源要重新量。
5. **版本基线**：`res-stock` 来自当前设备（任务给定官方 1.1.3）。如果设备做过 OTA 或恢复过更旧/更新固件，必须重新 pull 并重新生成，不能复用旧基线。
6. **`/tmp` 是易失的**：设备重启后 `/tmp/update.img` 消失；升级失败后不能靠它重试，需要重新 push。
7. **WiFi ADB 可能不自动恢复**：重启后 IP 可能变化，脚本有超时和提示，但最后仍要人工确认。
8. **防砖回滚依赖 `sys.zkapp.state`**：`Main.cpp` 在 `onEasyUIInit` 设置 `running`；如果新 App 在这之前崩溃，系统可能触发防砖回滚。不要删除或延迟该设置。
9. **无 OTA/远程升级/鉴权**：本镜像不提供 OTA；ADB 5555 可写设备，刷机时必须保证局域网可信。
10. **不要混用 `.so`**：`build/` 下可能残留多个 workspace 的旧库；正式镜像以 `dist/build-info.txt` 记录的输入与 hash 为准。
11. **`/tmp` 覆盖层不是升级**：`deploy-tmp.ps1` 只用于开发调试；已固化设备要更新版本必须重新刷 `/res`。
12. **SoftAP 未完成物理验证**：v2.1.2 的 AP 入口是长按中键 3 秒；物理长按尚未由用户确认。AP 与 STA 互斥，AP 期间不能上网。
13. **80 端口可能被占用**：80 bind 失败只打 warning，8080 不受影响，但必须手动输入 `:8080`。

---

## 9. 执行记录与复核清单

### 9.1 v2 执行记录（历史）

| 项目 | 记录 |
|---|---|
| 镜像生成 | 2026-10-22 17:07:48（`dist/build-info.txt`） |
| 固化时间 | 2026-10-22 17:08:00 左右（Asia/Shanghai） |
| 设备 | `192.168.31.124:5555`，`Zkswe_SSD21X_SPINOR`，官方 1.1.3，MCU V1.0.17 |
| 命令 | `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 -Image dist\update.img -Confirm` |
| 官方步骤 | `adb push` → `setprop sys.zkupgrade.flag 255` → `setprop sys.zkupgrade.dir /tmp` → `setprop ctl.restart zkswe` |
| 镜像 | `dist/update.img`，741,948 字节，SHA256 `03DCA727...1AAC7` |
| 结果 | ADB 断开/恢复；`/tmp` 为空；`/res` 新 `.so`；`/data` 保留；HTTP 200；冷启动 restore+SNTP |
| 本地证据 | `dist/build-info.txt`、`dist/update.img.sha256`、`build/verify/`（固化后回读） |

### 9.2 v2 执行前清单（已完成，历史）

- [x] 已确认最终 `libzkgui.so`：`dist/lib/libzkgui.so`，435,820 字节，SHA256 `123E1848...3DEC`。
- [x] 已用该 `.so` 在 `/tmp` 真机跑过核心流程（HTTP、倒计时、配置保存、时钟/时区）。
- [x] 已重新确认 `build/flash/res-stock` 的 225 文件 / 9,148,120 字节 / 指纹。
- [x] 已生成正式 `update.img`，并核对 `build-info.txt` 与 `.sha256`。
- [x] 已用 7-Zip 检查镜像内有全部 stock 资源，`EasyUI.cfg` 仍为 `/res`。
- [x] 已把 `build/device-backup/` 复制到仓库外离线介质，敏感文件未外传。
- [x] 已确认设备供电稳定，物理上可长按复位键。
- [x] 已接受最坏情况：复位恢复官方固件并清空 `/data`。
- [x] 已确认当前 IP、ADB 5555、`/tmp` 可用空间。
- [x] 已人工批准 `-Confirm`，未批准前只执行 DryRun。

### 9.3 v2 执行后清单（已完成，历史）

- [x] ADB 断开后恢复，设备正常启动。
- [x] `init.svc.zkswe = running`、`sys.zkapp.state = running`。
- [x] `/res/lib/libzkgui.so` = 435,820 字节，hash 匹配。
- [x] `/res/etc/EasyUI.cfg` 仍为 `/res` 路径，hash 匹配 stock。
- [x] `/res/ui/main.ftu` = 162 字节，hash 匹配。
- [x] `/tmp` 为空。
- [x] `/data/multitimer.conf`、`multitimer.wifi`、`preferences.json` 保留。
- [x] HTTP `/api/status` 返回 200。
- [x] 冷启动日志出现 restore + SNTP，时间校正。
- [x] 双目标切换、预警、回拨、时区、远目标、配置重启保持均验证。
- [x] 主机单测 92/92、网页自检 55/55（v2 当时）、构建 0 errors / 0 warnings。

---

## 10. 官方流程依据

| 内容 | 依据 |
|---|---|
| ADB 固化 4 条命令 | `%TEMP%\uclock-sdk\IDE使用说明\说明文档.md` “通过 ADB 固化应用程序”（L1165–L1172） |
| 制作升级镜像 / TF 卡 | 同文档 “制作升级镜像”（L1231–L1239）、“自动升级”（L1245–L1248） |
| 远程升级 | 同文档 “远程升级”（L1254–L1264） |
| 镜像工具与参数 | `docs/RESEARCH-tc002-wifi-deploy.md` §4.1：`zkswe_mkimg ... -comp xz`，`fsimg -i res:res.tmp -o update.img -p 0x000606` |
| 4 条 ADB 路径 | 同 §4.3（ADB 固化 / TF 卡 / 自动 / 远程） |
| `/data` 与 JFFS2 风险 | 同 §4.4；分区 mtd3=res 8 MiB、mtd6=data 8 MiB |
| 复位键恢复官方 + 清空 `/data` | 官方 `README_CN.md` §5（L223–L225）与 `Z21_TC002_Demo/README.md` L120：按住 USB-C 旁复位键上电，自动刷回官方固件；“约 5 秒 + 清空 `/data`”来自社区 install-tools / `docs/RESEARCH-tc002-wifi-deploy.md` §4.5，官方 README 未写 |
| 防砖标志 | 同 §4.6：`SystemProperties::setString("sys.zkapp.state","running")` |

> 说明：任务提到的“恢复官方固件章节”实际不在 `IDE使用说明/说明文档.md` 中；该文档包含“制作升级镜像”“通过 ADB 固化应用程序”，
> 而官方恢复说明位于仓库根 `README_CN.md` §5 与 `Z21_TC002_Demo/README.md`。本 Runbook 已把两处依据都列出。

本地关键文件：

- `docs/DESIGN-v2.md` §2/§9/§10（时间语义、文件所有权、明确不做）
- `docs/RESEARCH-tc002-wifi-deploy.md` §3/§4（ADB 部署、镜像、恢复）
- `build/device-backup/`（备份）
- `build/ide-tools/zkswe_mkimg.exe`、`fsimg.exe`（IDE 内置工具解出）
- `build/flash/res-stock/`（本次 pull 的 stock 基线）
- `build/verify/`（固化后从设备回读的关键文件）

---

## 11. v2.1.2 固化记录（历史）

> 执行时间：2026-10-22 19:05:00–19:06:00 左右（Asia/Shanghai）。
> 设备：`192.168.31.124:5555`，`Zkswe_SSD21X_SPINOR`（Z21），官方固件 1.1.3，
> MCU V1.0.17。镜像生成时间以 `dist/build-info.txt` 的 `generated_local` 为准：
> **2026-10-22T19:05:57.8611628+08:00**。

### 11.1 v2.1.2 产物与校验（历史）

| 文件 | 字节 | SHA256 |
|---|---:|---|
| `dist/lib/libzkgui.so` | **480,876** | `7A76240720B558CAA69FC7A9ABC6DBE2B66860B0FEE3FE0B738A0C229EB5DD9B` |
| `dist/update.img` | **762,428** | `A3C1655AD16C27A18D6D25DAC1835D265EB8256CD7E802EABFB99923132FE9B8` |
| `build/flash/image-work/res.tmp` | **761,856** | `95D53C0D46597F96FC09C61A9C07783C11C12757DE909632DD5D5B7AB631326E` |
| `dist/ui/main.ftu` | **162** | `CFD891ABA792AB2B73681A78A5FA09D69BF7C5A54857EC038493DD3958A6D799` |
| `dist/update.img.sha256` | 77 | `A3C1655A...2FE9B8  update.img` |
| `dist/build-info.txt` | 1,432 | 输入/基线/中间产物/最终镜像的 hash、大小、平台码与命令 |

**版本历史（同一天两次构建，字节数相同）**：

| 版本 | 构建 | `libzkgui.so` SHA256 | `update.img` SHA256 | `res.tmp` SHA256 | 设备 |
|---|---|---|---|---|---|
| v2.1.1 | 18:59:10 / 19:00:53 | `A9AE6B05...DAB55E` | `9378387A...59341` | `4B2A06F7...` | 19:01:00 刷入，已被取代 |
| **v2.1.2（历史）** | **19:05:26 / 19:05:57** | **`7A762407...B5DD9B`** | **`A3C1655A...2FE9B8`** | **`95D53C0D...`** | **19:06:00 刷入，已被 v3.0 取代** |

> v2.1.1 完整值（历史）：`libzkgui.so` =
> `A9AE6B054A299072AB4E6B53AE012B95ADE0C7F793A02B6AFBFF255103DAB55E`；
> `update.img` = `9378387A3B367FB6A914FF7AFE3E58DE70455ED65D448402B961CB48CE259341`；
> `res.tmp` = `4B2A06F7BACBA5984494744F5DA45AD7974921B2CE3A4B98AB53AE9B4DDD118B`。

- `update.img = res.tmp + 572` 字节官方包头，平台码 `0x000606`；payload 761,856 字节，
  占 8 MiB `/res` 分区的 **9.08%**。
- stock `/res` 基线仍是 **225 个文件、9,148,120 字节**，目录指纹
  `061F8B3C...19A5` 未变；镜像只替换 `lib/libzkgui.so` 与 `ui/main.ftu`。
- 镜像内 `etc/EasyUI.cfg` 仍为 stock 的 **318 字节**、
  SHA256 `E0C101F7E16DC8A1C8A6CAF9B73B47C393FAAF46C1F2DCE7D9FC8C8650215DCC`，
  路径是 `/res/lib`、`/res/ui`、`/res/tr`。
- `dist/EasyUI.cfg`（399 字节，`/tmp` 部署配置）仍只用于 `deploy-tmp.ps1`，不进入镜像。
- v2.1.2 相对 v2.1.1 的修复：`ConfigMode::exit()` 先 `setEnable(false)` 再清
  `mActive`；`connectWithSavedCredential()` 每轮检查 AP；进入 AP 时 LED 显示
  `TC002-Setup` + `12345678` 两行 10 秒。镜像布局、大小、平台码均未变。

### 11.2 生成与刷写

生成（19:05:51–19:05:57）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\make-image.ps1 `
  -SoPath dist\lib\libzkgui.so `
  -MainFtu native\MultiTimer\ui\main.ftu `
  -OutDir dist
```

刷写（19:06:00 左右，`-Confirm` 才写设备）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 `
  -Image dist\update.img -Confirm
```

脚本执行的官方流程（与 v2 相同）：

```bash
adb push ./update.img /tmp/update.img
adb shell setprop sys.zkupgrade.flag 255
adb shell setprop sys.zkupgrade.dir /tmp
adb shell setprop ctl.restart zkswe
```

本次 `libzkgui.so` 构建于 **19:05:26**（`build/v212-build.log`，
`Build Finished. 0 errors, 0 warnings.`）；`res.tmp`/`update.img` 生成于
19:05:51–19:05:57。设备回读文件 `build/verify/res-lib-current.so`（19:10:28 重新 pull）
SHA256 为 `7A762407...B5DD9B`，与 `dist/lib/libzkgui.so` 一致；
刷写前 `/tmp` 验证的 `build/verify/tmp-libzkgui.so`（19:00:45，v2.1.1 的 `.so`）
仅作历史，不再代表当前版本。

### 11.3 刷后证据（v2.1.2，历史）

| 检查项 | 实测 |
|---|---|
| `/tmp` | **为空**（`ls -l /tmp` 无输出） |
| `/res/lib/libzkgui.so` | **480,876 字节**，pull 回读 SHA256 `7A76240720B558CAA69FC7A9ABC6DBE2B66860B0FEE3FE0B738A0C229EB5DD9B` |
| `/res/etc/EasyUI.cfg` | 仍为 stock **318 字节** `/res` 配置（本轮未替换） |
| `/data` | **保留**（`multitimer.conf`、`multitimer.wifi`、`preferences.json` 未被本流程清空） |
| WiFi | 仍连接（SSID/密码不记录在仓库） |
| HTTP | `GET /api/status` **200**、`GET /api/wifi` **200**、`GET /api/config` **200** |
| 80→8080 | `GET http://192.168.31.124/` 返回 **302**，`Location: http://192.168.31.124:8080/` |
| 服务 | `init.svc.zkswe = running`、`sys.zkapp.state = running` |
| 本地回读证据 | `build/verify/res-lib-current.so`（hash 与 dist 一致，19:10:00 重新 pull） |

目标配置由用户通过网页维护；具体 `targets` 以设备 `GET /api/config` 为准，
本 Runbook 不对“用户当前有哪些目标”作断言。

### 11.4 与 v2 的差异（刷机角度）

- 镜像布局、平台码、572 字节包头、stock 基线、ADB 4 条命令完全不变；
  只是 `libzkgui.so` 从 435,820 字节增至 **480,876** 字节，`res.tmp` 从 741,376
  增至 **761,856** 字节（仍远低于 8 MiB）。
- 应用配置仍是 `/data/multitimer.conf` v2，新增可选 `ntpServer` 字段；
  WiFi 文件升级为 v2 列表（旧单对象自动兼容），不需要清 `/data` 迁移。
- 应用侧新增 SoftAP 配网与 80→8080 跳转；这两项不改变 `/res` 布局，也不影响
  官方升级器。
- v2.1.2 构建：`Build Finished. 0 errors, 0 warnings.`（19:05:26），产物
  `build/v212-workspace/MultiTimer/Release/libzkgui.so`；主机单测 **102/102**，
  网页自检 **62/62**。v2.1.1 的 `v211-workspace` 仅作历史。

### 11.5 v2.1.2 相关风险

1. **物理长按未由用户确认**：SoftAP 入口是长按中键 3 秒。代码、构建、
   AP IP（`192.168.100.1`）已实现/实测，v2.1.2 进入 AP 时 LED 会显示
   `TC002-Setup` + `12345678` 两行 10 秒；但**物理长按进入/退出尚未由用户确认**，
   刷机后请按 [`native/MultiTimer/README.md`](../native/MultiTimer/README.md#softap-配网v21)
   实测。不要把“AP IP 实测”写成“长按已验证”。
2. **AP 与 STA 互斥**：AP 开启时设备不能上网，NTP/网页对时可能失败；
   保存 WiFi 列表会先退出 AP 再重连。
3. **AP 固定密码与 15 分钟窗口**：`TC002-Setup` / `12345678` 写在固件里；
   窗口内附近知道密码的人也能连上配网页。用完建议长按退出。
4. **80 端口可能被占用**：占用时只告警，8080 不受影响，但必须手动输入 `:8080`。
5. **WiFi 列表仍有 8 个上限**，且没有主动扫描；切换网络后需要等待重连。
6. **回滚路径不变**：复位键恢复官方固件会清空 `/data`；`deploy-tmp.ps1 -Restore`
   只清 `/tmp` 覆盖层，不能恢复 stock `/res`。

### 11.6 复核命令（只读）

```powershell
$adb = ".\tools\platform-tools\adb.exe"
& $adb connect 192.168.31.124:5555
& $adb -s 192.168.31.124:5555 shell "ls -l /tmp /res/lib/libzkgui.so /res/etc/EasyUI.cfg"
& $adb -s 192.168.31.124:5555 pull /res/lib/libzkgui.so build\verify\res-lib-current.so
Get-FileHash build\verify\res-lib-current.so -Algorithm SHA256
& $adb -s 192.168.31.124:5555 shell "ls -l /data/multitimer.conf /data/multitimer.wifi /data/preferences.json"
Invoke-WebRequest -UseBasicParsing http://192.168.31.124:8080/api/status |
  Select-Object -ExpandProperty Content
curl.exe -s -o NUL -D - http://192.168.31.124/
```

> 设备上的 `/bin/sh` 没有 `sha256sum`（实测 `sha256sum: not found`），所以先 pull 到
> 本地再 `Get-FileHash`；不要把 `build/device-backup/res-stock.img` 当 `update.img` 刷入。

---

## 12. v3.0 固化记录（历史）

> 本节为 v3.0 历史记录。**v3.2 已让 B 站设备直连可用（OpenSSL 1.1.1w，见 §13）**；
> §12 中“B 站直连不可用/必须走 bridge”的描述仅代表 v3.0。

> 执行时间：镜像生成 **2026-10-22 20:13:55**（Asia/Shanghai），随后通过官方 ADB
> 流程刷入 `/res` 并完成真机验证。源码冻结于 **19:57:40**，最终交叉编译
> **19:57:11**（`build/v3-final-build.log`：`Build Finished. 0 errors, 0 warnings.`）。
> 设备：`192.168.31.124:5555`，`Zkswe_SSD21X_SPINOR`（Z21），官方固件 1.1.3，
> MCU V1.0.17。镜像生成时间以 `dist/build-info.txt` 的 `generated_local` 为准：
> **2026-10-22T20:13:55.2668398+08:00**。

### 12.1 v3.0 产物与校验

| 文件 | 字节 | SHA256 |
|---|---:|---|
| `dist/lib/libzkgui.so` | **718,436** | `A4B1D7E8FAB7803776A1C410E5F5128C28B3354541DD272BA1AAE1C500E4B99F` |
| `dist/update.img` | **864,828** | `7058D4C0014912D5EE5AD241956B8C8A25A5CD3929F647510684218A9DF57CAC` |
| `build/flash/image-work/res.tmp` | **864,256** | `952924B446BE4CB072A0DF32E5FA92D7547D43D6F85BD77AE257D1F66D03960D` |
| `dist/ui/main.ftu` | **162** | `CFD891ABA792AB2B73681A78A5FA09D69BF7C5A54857EC038493DD3958A6D799` |
| `dist/update.img.sha256` | 77 | `7058D4C0...7CAC  update.img` |
| `dist/build-info.txt` | 1,432 | 输入/基线/中间产物/最终镜像的 hash、大小、平台码与命令 |

- `update.img = res.tmp`（864,256 字节）+ **572 字节**官方升级包头，平台码
  `0x000606`；payload 占 8 MiB `/res` 分区的 **10.30%**（v2.1.2 为 9.08%）。
- stock `/res` 基线未变：**225 个文件、9,148,120 字节**，目录指纹
  `061F8B3C...19A5`；镜像只替换 `lib/libzkgui.so` 与 `ui/main.ftu`，
  staged 树 SHA256 `C764CCE101B7E36C5EB91128D4627DE819DE179DCAFEBD361FE313CD50E22FEC`。
- 镜像内 `etc/EasyUI.cfg` 仍为 stock 的 **318 字节**、
  SHA256 `E0C101F7E16DC8A1C8A6CAF9B73B47C393FAAF46C1F2DCE7D9FC8C8650215DCC`；
  `dist/EasyUI.cfg`（399 字节，`/tmp` 部署配置）不进入镜像。
- 构建与测试（同一冻结源码）：交叉编译 `0 errors / 0 warnings`（19:57:11）；
  QA 在独立 workspace 重编得到**相同 SHA256**（可复现）；主机单测 **209/209**、
  网页自检 **90/90**；`web-src/index.html` 181,834 字节重新生成的内嵌 gzip
  45,595 字节与 `src/web/editor_gz.h` 一致。
- 历史版本（同一天）：v2.1.2 `libzkgui.so` 480,876 / `7A762407...B5DD9B`、
  `update.img` 762,428 / `A3C1655A...2FE9B8`（见 §11，已被本版取代）；
  v2.1.1（19:00:53）/ v2（17:08:00）见 §11.1、§9.1。

### 12.2 生成与刷写

生成（20:13:51–20:13:55）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\make-image.ps1 `
  -SoPath dist\lib\libzkgui.so `
  -MainFtu native\MultiTimer\ui\main.ftu `
  -OutDir dist
```

刷写（人工复核后，显式 `-Confirm` 才写设备）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 `
  -Image dist\update.img -Confirm
```

脚本执行的官方流程（与 v2/v2.1 相同，未改）：

```bash
adb push ./update.img /tmp/update.img
adb shell setprop sys.zkupgrade.flag 255
adb shell setprop sys.zkupgrade.dir /tmp
adb shell setprop ctl.restart zkswe
```

- `dist/lib/libzkgui.so` 来自最终源码构建（`build/v3-final-workspace`，19:57:11）；
  `res.tmp`/`update.img` 生成于 20:13:51–20:13:55，随后刷入 `/res`；
- 刷后真机验证采样时间为 **20:15:46**（分屏页 `/api/status` 的 `text`）；
- 固化后 `/res/lib/libzkgui.so` 回读 **718,436 字节**，SHA256
  `A4B1D7E8...B99F`，与 `dist/lib/libzkgui.so` 一致。

### 12.3 刷后证据（v3.0，历史）

| 检查项 | 实测 |
|---|---|
| `/tmp` | **为空** |
| `/res/lib/libzkgui.so` | **718,436 字节**，回读 SHA256 `A4B1D7E8FAB7803776A1C410E5F5128C28B3354541DD272BA1AAE1C500E4B99F` |
| `/res/etc/EasyUI.cfg` | 仍为 stock **318 字节** `/res` 配置（本轮未替换） |
| `/data` | **保留**（`multitimer.conf`、`multitimer.wifi`、`preferences.json` 未被本流程清空） |
| 配置迁移 | 真机日志 `migrated legacy config to v3`：配置自动从 v2 升级为 v3（只迁移/保存一次），保留时区、颜色、预警、目标、NTP 与页面结构 |
| WiFi | 仍连接（SSID/密码不记录在仓库） |
| HTTP | `GET /api/status`、`GET /api/config`、`/api/page`、`GET /api/live/status` 正常 |
| 80→8080 | `GET http://192.168.31.124/` 返回 **302**，`Location: http://192.168.31.124:8080/` |
| 服务 | `init.svc.zkswe = running`、`sys.zkapp.state = running` |
| 页面系统 | 通过 `POST /api/page` 切换验证：`p0` clock；`p1` 8+8 分屏（`text=20:15:46`、`text2=00:14:14`，同一秒）；`p2` countdown（`00:14:13`）；`p3` live（`LIVE`/`online`） |
| 倒计时 | 临时目标到点后自动切到下一个未来目标（`remainSeconds=870`）；临时目标已删除、用户原有目标已恢复 |
| 直播 | `hybrid` + bridge `http://192.168.31.66:8081/live`：B 站示例房间与抖音 19 位示例房间均 `state=ok`（抖音 0 值为有效值）；`/api/live/status` 中继 `reachable=true` |
| 网页 v3 | 真机 Edge headless 加载成功（页面列表/直播源控件）；首屏 gzip **45,595 字节** |
| B 站直连 | **当前固件不可用**（设备 libssl `SSL_connect` 141640BF `no protocols available`，所有 HTTPS 站点）；实际 B 站数据经 bridge 获取 |

目标配置由用户通过网页维护；具体 `targets` 以设备 `GET /api/config` 为准，
本 Runbook 不对“用户当前有哪些目标”作断言。

### 12.4 与 v2.1.2 的差异（刷机角度）

- 镜像布局、平台码 `0x000606`、572 字节官方包头、stock 基线（225 文件）、
  ADB 4 条命令完全不变；仍只替换 `lib/libzkgui.so` 与 `ui/main.ftu`。
- `libzkgui.so` 从 480,876 字节增至 **718,436** 字节（+约 232 KB，含页面系统/
  直播/网页资源）；`res.tmp` 从 761,856 增至 **864,256** 字节，
  payload 占比 9.08% → **10.30%**（仍远低于 8 MiB）。
- 应用配置 `/data/multitimer.conf` 从 v2 自动迁移为 **v3**（保留目标/时区/颜色/
  预警/NTP，补 `pages`/`live`/`display`/`colors.dim` 与 3 个默认页面）；不需要清
  `/data`，也不要手工改配置文件。
- v3 新增页面系统与直播配置；`live.source` 默认 `off`，实际部署用 `bridge` 或
  `hybrid`+`live.url`（三平台都经 bridge）。B 站设备直连代码保留但当前固件不可用。
- 回滚注意：v3 配置**不能**回滚给 v2 固件使用（v2 会因 `version!=2` 按默认配置运行）；
  回滚前先 `adb pull /data/multitimer.conf` 备份。
- QA 结论：首轮 P0=0/P1=0/P2=10（[`QA-v3-initial.md`](QA-v3-initial.md)），
  最终回归确认独立重编逐字节一致、P2-1/P2-3 已修
  （[`QA-v3-final.md`](QA-v3-final.md)）。

### 12.5 v3.0 相关风险（历史）

1. **B 站设备直连在当前固件不可用（核心限制）**：设备 `/lib/libssl.so.1.1` 的
   TLS client 无可用协议（`SSL_connect` 141640BF `no protocols available`，
   所有 HTTPS 站点；raw server 收到 0 字节 ClientHello）。实际数据**必须**由
   bridge 提供；`live.url` 为空时 B 站会尝试直连并失败（`--`，60 分钟退避）。
   不要写成“B 站直连可用”。
2. **bridge 明文 + 无鉴权**：只适合可信局域网，不要公网映射；设备只接受
   `http://` bridge 地址（不支持 https）。
3. **抖音限流 / 快手 Playwright**：抖音匿名路径会被 IP 级限流（建议房间 ≤2、
   `refreshSec` ≥30 或使用 19 位 `room_id`）；快手无浏览器时返回
   `kuaishou_requires_browser`。
4. **60 分钟直连退避对所有直连失败类型生效**（QA P2-12）：当前固件 TLS 恒失败
   无影响；未来换带可用 TLS 的固件时需注意瞬时失败会导致 60 分钟不再直连。
5. **物理输入仍未由用户确认**：旋钮 CW/CCW 与中键短按/长按手感、物理长按中键
   3 秒进入/退出 SoftAP（v2 遗留）尚未确认；请按
   [`native/MultiTimer/README.md`](../native/MultiTimer/README.md) 实测。
6. **长稳未验证**：24h+ 内存/线程/HTTP 稳定性、8 个 WiFi 逐个切换、自定义 NTP
   真机校时等仍未验证（清单见
   [`STATE-2026-10-22-v3.md`](STATE-2026-10-22-v3.md) §10.2）。
7. **回滚路径不变**：复位键恢复官方固件会清空 `/data`；`deploy-tmp.ps1 -Restore`
   只清 `/tmp` 覆盖层，不能恢复 stock `/res`。
8. **其他 QA P2 未修项**：`/api/live/status` 字段与契约简表差异、固件/网页模拟器
   空槽占位差异、设计文档旧版式等，见
   [`QA-v3-final.md`](QA-v3-final.md) §7。

### 12.6 复核命令（只读）

```powershell
$adb = ".\tools\platform-tools\adb.exe"
& $adb connect 192.168.31.124:5555
& $adb -s 192.168.31.124:5555 shell "ls -l /tmp /res/lib/libzkgui.so /res/etc/EasyUI.cfg"
& $adb -s 192.168.31.124:5555 pull /res/lib/libzkgui.so build\verify\res-lib-v3.so
Get-FileHash build\verify\res-lib-v3.so -Algorithm SHA256   # 应为 A4B1D7E8...B99F
& $adb -s 192.168.31.124:5555 shell "ls -l /data/multitimer.conf /data/multitimer.wifi /data/preferences.json"
Invoke-WebRequest -UseBasicParsing http://192.168.31.124:8080/api/status |
  Select-Object -ExpandProperty Content
Invoke-WebRequest -UseBasicParsing http://192.168.31.124:8080/api/config |
  Select-Object -ExpandProperty Content
Invoke-WebRequest -UseBasicParsing http://192.168.31.124:8080/api/live/status |
  Select-Object -ExpandProperty Content
curl.exe -s -o NUL -D - http://192.168.31.124/
```

> 设备上的 `/bin/sh` 没有 `sha256sum`（实测 `sha256sum: not found`），所以先 pull
> 到本地再 `Get-FileHash`；不要把 `build/device-backup/res-stock.img` 当
> `update.img` 刷入。镜像与发布包 hash 见 [`dist/README.md`](../dist/README.md)。

---

## 13. v3.2 固化记录（历史）

> 本节为 v3.2 历史记录。**v3.3 起无有效目标显示 `NO COUNTDOWN`（不再显示日期），
> 并新增 `datetime` 页；B 站直连/正计时/按键/IP 从 v3.2 起可用（见 §14）**。

> 执行时间：镜像生成 **2026-10-22 21:20:51**（Asia/Shanghai），随后通过官方 ADB
> 流程刷入 `/res` 并完成真机验证。源码构建 **21:20:27**（`build/v3f-build.log`：
> `Build Finished. 0 errors, 0 warnings.`）。设备：`192.168.31.124:5555`，
> `Zkswe_SSD21X_SPINOR`（Z21），官方固件 1.1.3，MCU V1.0.17。
> 镜像生成时间以 `dist/build-info.txt` 的 `generated_local=2026-10-22T21:20:51.5777956+08:00` 为准。
> v3.2 相对 v3.0 的关键变化：**镜像新增 OpenSSL 1.1.1w → B 站设备直连可用**、
> 正计时、中键单击 IP / 右键长按 SoftAP、IP 全屏滚动、网页自动地址。

### 13.1 v3.2 产物与校验

| 文件 | 字节 | SHA256 |
|---|---:|---|
| `dist/lib/libzkgui.so` | **722,532** | `09753AADE074451BF03EBF33413813188301375062797D2C5079EB3138C67DAF` |
| `dist/update.img` | **1,942,076** | `8C098B91315DF1E27192EC94B772D9E08A8298D205EAC973A9E876998434AD59` |
| `build/flash/image-work/res.tmp` | **1,941,504** | `02A3B3FA5472AE99637037FB9A140382D9B0D197A3EAECB209E20FA1701EBBBF` |
| `dist/ui/main.ftu` | **162** | `CFD891ABA792AB2B73681A78A5FA09D69BF7C5A54857EC038493DD3958A6D799` |
| `dist/update.img.sha256` | 77 | `8C098B91...AD59  update.img` |
| `dist/build-info.txt` | 1,628 | 输入/基线/extra/中间产物/最终镜像的 hash、大小、平台码与命令 |

- `update.img = res.tmp`（1,941,504 字节）+ **572 字节**官方升级包头，平台码
  `0x000606`；payload 占 8 MiB `/res` 分区的 **23.15%**（v3.0 为 10.30%）。
- stock `/res` 基线未变：**225 个文件、9,148,120 字节**，目录指纹
  `061F8B3C...19A5`；镜像共 **227 个文件** = stock 225 + `res-extra` 2，
  替换 `lib/libzkgui.so` 与 `ui/main.ftu`，新增 `/res/lib/libssl.so.1.1` +
  `/res/lib/libcrypto.so.1.1`。
- `res-extra` 2 个文件（OpenSSL 1.1.1w，Debian 11 armhf）：
  - `libssl.so.1.1`：352,540 B / `BE719492E3D2E1987C4CDFF191C929576B14E97B182D4328458E5820FE0B909D`
  - `libcrypto.so.1.1`：1,737,128 B / `5AFCFD7722883313678BADCC34828FC90C2F34ED9F26D0A68A8CC4062000FEBC`
  - `extra_tree_sha256=E17B59DE...9C5D`，`staged_tree_sha256=75A28A35...98E4`。
- 构建与测试（同一份 v3.2 源码）：交叉编译 `0 errors / 0 warnings`（21:20:27）；
  主机单测 **210/210**、网页自检 **90/90**、bridge 单测 **40/40**；
  v3.2 暂无独立 QA 报告（v3.0 QA 报告为历史基线）。
- 历史版本（同一天）：v3.0 `libzkgui.so` 718,436 / `A4B1D7E8...`、
  `update.img` 864,828 / `7058D4C0...`（见 §12，已被本版取代）；
  v2.1.2 / v2.1.1 / v2 见 §11、§9.1。

### 13.2 生成与刷写

生成（21:20:27–21:20:51；`make-image.ps1` 默认叠加 `res-extra`）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\make-image.ps1 `
  -SoPath build\v3f-workspace\MultiTimer\Release\libzkgui.so `
  -MainFtu native\MultiTimer\ui\main.ftu `
  -OutDir dist
```

刷写（人工复核后，显式 `-Confirm` 才写设备）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 `
  -Image dist\update.img -Confirm
```

脚本执行的官方流程（与 v2/v2.1/v3.0 相同，未改）：

```bash
adb push ./update.img /tmp/update.img
adb shell setprop sys.zkupgrade.flag 255
adb shell setprop sys.zkupgrade.dir /tmp
adb shell setprop ctl.restart zkswe
```

- `libzkgui.so` 来自 `build/v3f-workspace`（21:20:27 构建），`res.tmp`/`update.img`
  生成于 21:20:25–21:20:51，随后刷入 `/res`；
- 固化后 `/res/lib/libzkgui.so` 回读 **722,532 字节**，SHA256
  `09753AAD...7DAF`（`build/verify/flashed-v3f.so`，21:31:00 回读），与 `dist` 一致；
- `/res/lib/libssl.so.1.1`、`libcrypto.so.1.1` 已随镜像写入（OpenSSL 1.1.1w）。

### 13.3 刷后证据（v3.2，历史）

| 检查项 | 实测 |
|---|---|
| `/tmp` | **为空** |
| `/res/lib/libzkgui.so` | **722,532 字节**，回读 SHA256 `09753AADE074451BF03EBF33413813188301375062797D2C5079EB3138C67DAF` |
| `/res/lib/libssl.so.1.1` / `libcrypto.so.1.1` | 已安装（OpenSSL 1.1.1w；B 站直连依赖） |
| `/res/etc/EasyUI.cfg` | 仍为 stock **318 字节** `/res` 配置（本轮未替换） |
| `/data` | **保留**（`multitimer.conf`、`multitimer.wifi`、`preferences.json` 未被本流程清空） |
| 配置 | 保持 v3；v2→v3 迁移已在 v3.0 完成，v3.2 增量 `colors.countup` 缺省补 `#00BFFF`；用户目标/直播配置保留 |
| WiFi | 仍连接（SSID/密码不记录在仓库） |
| HTTP | `GET /api/status`、`GET /api/config`、`POST /api/page`、`GET /api/live/status` 正常 |
| 80→8080 | `GET http://192.168.31.124/` 返回 **302**，`Location: http://192.168.31.124:8080/` |
| 服务 | `init.svc.zkswe = running`、`sys.zkapp.state = running` |
| **正计时** | 真机 `/api/status`：分屏页 `mode=countup`、`text2=00:19:32`（用户目标过期后累计；具体目标不在此断言） |
| **B 站设备直连** | **停止 bridge 后**，B 站示例房间（测试用）仍 `state=ok`（HTTPS 直连取到数据）；抖音/快手继续走 bridge |
| 网页 v3.2 | Edge 打开设备页面加载成功：自动地址、无 IP 输入框、含正计时颜色；首屏 gzip 约 45 KB 级 |
| 按键/IP | 中键单击显示全屏 IP（12s，无前缀，`/api/status` `overlay=true`）；右键长按进/出 SoftAP（LED 观感待用户确认） |

目标/直播配置由用户通过网页维护；具体 `targets`/房间以设备 `GET /api/config` 为准，
本 Runbook 不对“用户当前有哪些目标/房间”作断言。

### 13.4 与 v3.0 的差异（刷机角度）

- 镜像布局、平台码 `0x000606`、572 字节官方包头、stock 基线（225 文件）、ADB 4 条
  命令完全不变；仍替换 `lib/libzkgui.so` 与 `ui/main.ftu`，**新增** 2 个 `/res/lib` 文件。
- `libzkgui.so` 从 718,436 增至 **722,532** 字节；`res.tmp` 从 864,256 增至
  **1,941,504** 字节（+1,077,248，主要是 OpenSSL 1.1.1w 两个库），payload 占比
  10.30% → **23.15%**（仍远低于 8 MiB）。
- **B 站直连从“不可用/只能走 bridge”变为可用**：系统 1.1.0i 不可用，镜像自带
  1.1.1w；`hybrid` 下 B 站直连、失败回退 bridge，抖音/快手走 bridge。
- 应用行为：正计时（目标过期后）、`colors.countup`、中键单击 IP（全屏滚动）、
  右键长按 SoftAP、网页自动地址（不再保存 IP 列表）、移除 `display` blank UI。
- 配置：v3.2 与 v3.0 配置兼容（新增 `colors.countup` 可缺省）；回滚 v3.0 固件会
  忽略该字段。回滚到 v2 固件前先备份 `/data/multitimer.conf`。

### 13.5 v3.2 相关风险

1. **B 站直连依赖镜像的 OpenSSL 1.1.1w**：若 `/res/lib/libssl.so.1.1` /
   `libcrypto.so.1.1` 缺失/被替换，直连回退系统 1.1.0i（无 TLS client）失败，
   只能走 bridge。升级镜像时不要漏掉 `res-extra` 的 2 个文件。
2. **OpenSSL 1.1.1w 已 EOL（2023-09）**：只用于 B 站直连；关注安全公告，升级时
   保持文件名/SONAME，并同步 `third_party/openssl-armhf/README.md`、镜像 hash 与文档。
3. **镜像体积**：payload 23.15%（1,941,504 B），仍在预算内；后续不要随意加大库。
4. **B 站直连长期稳定性**：接口/证书/设备时间异常都会失败；失败回退 bridge 并
   60 分钟退避（`live.url` 非空时）。抖音/快手仍依赖 bridge（明文无鉴权，只可信局域网）。
5. **物理按键观感未确认**：中键单击 IP、右键长按 3s SoftAP 的 LED 观感与手感需用户确认。
6. **网页文案已更新**：直播数据源下拉/提示为“B 站设备直连 + 抖音/快手中继”，
   与 v3.2 行为一致（最终 v3f 起）。
7. **QA 报告滞后**：`docs/QA-v3-*.md` 是 v3.0 基线；v3.2 暂无独立 QA 报告。
8. **回滚路径不变**：复位键恢复官方固件会清空 `/data`；`deploy-tmp.ps1 -Restore`
   只清 `/tmp` 覆盖层，不能恢复 stock `/res`；`/tmp` 部署也不带 OpenSSL 库，
   B 站直连只有在 `/res` 固化后才可用。

### 13.6 复核命令（只读）

```powershell
$adb = ".\tools\platform-tools\adb.exe"
& $adb connect 192.168.31.124:5555
& $adb -s 192.168.31.124:5555 shell "ls -l /tmp /res/lib/libzkgui.so /res/lib/libssl.so.1.1 /res/lib/libcrypto.so.1.1 /res/etc/EasyUI.cfg"
& $adb -s 192.168.31.124:5555 pull /res/lib/libzkgui.so build\verify\res-lib-v32.so
Get-FileHash build\verify\res-lib-v32.so -Algorithm SHA256   # 应为 09753AAD...7DAF
& $adb -s 192.168.31.124:5555 shell "ls -l /data/multitimer.conf /data/multitimer.wifi /data/preferences.json"
Invoke-WebRequest -UseBasicParsing http://192.168.31.124:8080/api/status |
  Select-Object -ExpandProperty Content
Invoke-WebRequest -UseBasicParsing http://192.168.31.124:8080/api/config |
  Select-Object -ExpandProperty Content
Invoke-WebRequest -UseBasicParsing http://192.168.31.124:8080/api/live/status |
  Select-Object -ExpandProperty Content
curl.exe -s -o NUL -D - http://192.168.31.124/
```

> 设备上的 `/bin/sh` 没有 `sha256sum`（实测 `sha256sum: not found`），所以先 pull
> 到本地再 `Get-FileHash`；不要把 `build/device-backup/res-stock.img` 当
> `update.img` 刷入。镜像与发布包 hash 见 [`dist/README.md`](../dist/README.md)。

---

## 14. v3.3 固化记录（当前）

> 执行时间：镜像生成 **2026-10-22 21:49:28**（Asia/Shanghai），随后通过官方 ADB
> 流程刷入 `/res` 并完成真机验证。源码构建 **21:49:02**（`build/v3g-build.log`：
> `Build Finished. 0 errors, 0 warnings.`）。设备：`192.168.31.124:5555`，
> `Zkswe_SSD21X_SPINOR`（Z21），官方固件 1.1.3，MCU V1.0.17。
> 镜像生成时间以 `dist/build-info.txt` 的 `generated_local=2026-10-22T21:49:28.5950445+08:00` 为准。
> v3.3 相对 v3.2 的关键变化：**无目标显示 `NO COUNTDOWN`（不再显示日期）**、
> **新增 `datetime` 页（时间大字 + 日期小字）**；镜像大小/布局/OpenSSL 依赖不变。

### 14.1 v3.3 产物与校验

| 文件 | 字节 | SHA256 |
|---|---:|---|
| `dist/lib/libzkgui.so` | **722,532** | `1A4959919B126AC35CE673BF14799A83CFE767063CA61A17896E882E07B8086E` |
| `dist/update.img` | **1,942,076** | `8B2BCF20A71202610FAC6CA9C2A3D339B8D457E2C48D358B7C791933114C16B4` |
| `build/flash/image-work/res.tmp` | **1,941,504** | `92E2209AB6FAFC9456737C858D3E26DB95B4BF17F0A271C543B9C2D5C5159160` |
| `dist/ui/main.ftu` | **162** | `CFD891ABA792AB2B73681A78A5FA09D69BF7C5A54857EC038493DD3958A6D799` |
| `dist/update.img.sha256` | 77 | `8B2BCF20...C16B4  update.img` |
| `dist/build-info.txt` | 1,628 | 输入/基线/extra/中间产物/最终镜像的 hash、大小、平台码与命令 |

- `update.img = res.tmp`（1,941,504 字节）+ **572 字节**官方升级包头，平台码
  `0x000606`；payload 占 8 MiB `/res` 分区的 **23.15%**。
- stock `/res` 基线未变：**225 个文件、9,148,120 字节**，目录指纹
  `061F8B3C...19A5`；镜像共 **227 个文件** = stock 225 + `res-extra` 2，
  替换 `lib/libzkgui.so` 与 `ui/main.ftu`，新增 `/res/lib/libssl.so.1.1` +
  `/res/lib/libcrypto.so.1.1`。
- `res-extra` 2 个文件（OpenSSL 1.1.1w，Debian 11 armhf）：
  - `libssl.so.1.1`：352,540 B / `BE719492E3D2E1987C4CDFF191C929576B14E97B182D4328458E5820FE0B909D`
  - `libcrypto.so.1.1`：1,737,128 B / `5AFCFD7722883313678BADCC34828FC90C2F34ED9F26D0A68A8CC4062000FEBC`
  - `extra_tree_sha256=E17B59DE...9C5D`，`staged_tree_sha256=75A28A35...98E4`。
- 构建与测试（同一份 v3.3 源码）：交叉编译 `0 errors / 0 warnings`（21:49:02）；
  主机单测 **211/211**（PageRenderer 44，新增 NO COUNTDOWN/datetime 用例）、
  网页自检 **90/90**、bridge 单测 **40/40**；v3.2/v3.3 暂无独立 QA 报告。
- 历史版本（同一天）：v3.2 `libzkgui.so` 722,532 / `09753AAD...`、
  `update.img` 1,942,076 / `8C098B91...`（见 §13，已被本版取代）；
  v3.0 / v2.1.2 见 §12 / §11。

### 14.2 生成与刷写

生成（21:49:25–21:49:28；`make-image.ps1` 默认叠加 `res-extra`）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\make-image.ps1 `
  -SoPath build\v3g-workspace\MultiTimer\Release\libzkgui.so `
  -MainFtu native\MultiTimer\ui\main.ftu `
  -OutDir dist
```

刷写（人工复核后，显式 `-Confirm` 才写设备）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-update.ps1 `
  -Image dist\update.img -Confirm
```

脚本执行的官方流程（与 v2/v2.1/v3.0/v3.2 相同，未改）：

```bash
adb push ./update.img /tmp/update.img
adb shell setprop sys.zkupgrade.flag 255
adb shell setprop sys.zkupgrade.dir /tmp
adb shell setprop ctl.restart zkswe
```

- `libzkgui.so` 来自 `build/v3g-workspace`（21:49:02 构建），`res.tmp`/`update.img`
  生成于 21:49:25–21:49:28，随后刷入 `/res`；
- 固化后 `/res/lib/libzkgui.so` 回读 **722,532 字节**，SHA256
  `1A495991...8086E`（`build/verify/flashed-v3g.so`，22:01:00 回读），与 `dist` 一致；
- `/res/lib/libssl.so.1.1`、`libcrypto.so.1.1` 仍在（OpenSSL 1.1.1w）。

### 14.3 刷后证据（v3.3，当前）

| 检查项 | 实测 |
|---|---|
| `/tmp` | **为空** |
| `/res/lib/libzkgui.so` | **722,532 字节**，回读 SHA256 `1A4959919B126AC35CE673BF14799A83CFE767063CA61A17896E882E07B8086E` |
| `/res/lib/libssl.so.1.1` / `libcrypto.so.1.1` | 已安装（OpenSSL 1.1.1w；B 站直连依赖） |
| `/res/etc/EasyUI.cfg` | 仍为 stock **318 字节** `/res` 配置（本轮未替换） |
| `/data` | **保留**（`multitimer.conf`、`multitimer.wifi`、`preferences.json` 未被本流程清空） |
| 配置 | 保持 v3；用户目标/直播配置保留；`colors.countup` 缺省补默认；用户新增的 datetime 页保留 |
| WiFi | 仍连接（SSID/密码不记录在仓库） |
| HTTP | `GET /api/status`、`GET /api/config`、`POST /api/page`、`GET /api/live/status` 正常 |
| 80→8080 | `GET http://192.168.31.124/` 返回 **302**，`Location: http://192.168.31.124:8080/` |
| 服务 | `init.svc.zkswe = running`、`sys.zkapp.state = running` |
| **NO COUNTDOWN** | 真机 `/api/status`：无目标 p1 分屏 `text2=NO COUNTDOWN`；p2 倒计时页 `text=NO COUNTDOWN`（`mode=clock`） |
| **datetime 页** | 设备新增 datetime 页（p4）实测 `text=22:01:27`、`text2=2026-10-22` |
| **B 站设备直连** | **停止 bridge 后**，B 站示例房间（测试用）仍 `state=ok`（HTTPS 直连取到数据）；抖音/快手继续走 bridge |
| 网页 v3.3 | Edge 打开设备页面加载成功：自动地址、无 IP 输入框、页面类型含“日期 + 时间”、含正计时颜色 |
| 按键/IP | 中键单击显示全屏 IP（12s，无前缀，`/api/status` `overlay=true`）；右键长按进/出 SoftAP（LED 观感待用户确认） |

目标/直播配置由用户通过网页维护；具体 `targets`/房间/页数以设备 `GET /api/config` 为准，
本 Runbook 不对“用户当前有哪些目标/房间”作断言。

### 14.4 与 v3.2 的差异（刷机角度）

- 镜像布局、平台码 `0x000606`、572 字节官方包头、stock 基线（225 文件）、ADB 4 条
  命令、文件数（227）与 payload（23.15%）**完全不变**；`libzkgui.so` 大小仍 722,532 B。
- **行为变化**：无有效目标时 `NO COUNTDOWN`（dim，分屏下半/倒计时页）替代原日期；
  新增页面类型 `datetime`（时间 scale2 baseline10 + 日期 scale1 baseline15 dim）。
- 配置：v3.3 与 v3.2/v3.0 兼容（新增页面类型/仍为 version 3；datetime 仅在 v3.3+ 有效）。
- 测试：主机单测 210→**211**（PageRenderer 44），网页仍 **90/90**。
- 回滚 v3.2 固件会忽略 datetime 页/显示日期；回滚前先备份 `/data/multitimer.conf`。

### 14.5 v3.3 相关风险

1. **B 站直连依赖镜像的 OpenSSL 1.1.1w**：若 `/res/lib/libssl.so.1.1` /
   `libcrypto.so.1.1` 缺失/被替换，直连回退系统 1.1.0i（无 TLS client）失败，
   只能走 bridge。升级镜像时不要漏掉 `res-extra` 的 2 个文件。
2. **OpenSSL 1.1.1w 已 EOL（2023-09）**：只用于 B 站直连；关注安全公告，升级时
   保持文件名/SONAME，并同步 `third_party/openssl-armhf/README.md`、镜像 hash 与文档。
3. **B 站直连长期稳定性**：接口/证书/设备时间异常都会失败；失败回退 bridge 并
   60 分钟退避（`live.url` 非空时）。抖音/快手仍依赖 bridge（明文无鉴权，只可信局域网）。
4. **datetime/NO COUNTDOWN 是新语义**：旧网页/脚本若假定“无目标日期”或只认 4 种
   type 需要同步更新；当前网页已支持 5 种并显示 NO COUNTDOWN。
5. **物理按键观感未确认**：中键单击 IP、右键长按 3s SoftAP 的 LED 观感与手感需用户确认。
6. **QA 报告滞后**：`docs/QA-v3-*.md` 是 v3.0 基线；v3.2/v3.3 暂无独立 QA 报告。
7. **回滚路径不变**：复位键恢复官方固件会清空 `/data`；`deploy-tmp.ps1 -Restore`
   只清 `/tmp` 覆盖层，不能恢复 stock `/res`；`/tmp` 部署也不带 OpenSSL 库，
   B 站直连只有在 `/res` 固化后才可用。

### 14.6 复核命令（只读）

```powershell
$adb = ".\tools\platform-tools\adb.exe"
& $adb connect 192.168.31.124:5555
& $adb -s 192.168.31.124:5555 shell "ls -l /tmp /res/lib/libzkgui.so /res/lib/libssl.so.1.1 /res/lib/libcrypto.so.1.1 /res/etc/EasyUI.cfg"
& $adb -s 192.168.31.124:5555 pull /res/lib/libzkgui.so build\verify\res-lib-v33.so
Get-FileHash build\verify\res-lib-v33.so -Algorithm SHA256   # 应为 1A495991...8086E
& $adb -s 192.168.31.124:5555 shell "ls -l /data/multitimer.conf /data/multitimer.wifi /data/preferences.json"
Invoke-WebRequest -UseBasicParsing http://192.168.31.124:8080/api/status |
  Select-Object -ExpandProperty Content
Invoke-WebRequest -UseBasicParsing http://192.168.31.124:8080/api/config |
  Select-Object -ExpandProperty Content
Invoke-WebRequest -UseBasicParsing http://192.168.31.124:8080/api/live/status |
  Select-Object -ExpandProperty Content
curl.exe -s -o NUL -D - http://192.168.31.124/
```

> 设备上的 `/bin/sh` 没有 `sha256sum`（实测 `sha256sum: not found`），所以先 pull
> 到本地再 `Get-FileHash`；不要把 `build/device-backup/res-stock.img` 当
> `update.img` 刷入。镜像与发布包 hash 见 [`dist/README.md`](../dist/README.md)。
