# TC002（Z21 / 52×16）WiFi 与部署方式调研报告

- 调研日期：2026-10-22
- 调研性质：**只读**。未修改 `native/MultiTimer/src`、`web-src` 或任何应用代码，仅新增本文件。
- 目标：校验我们 App（替换官方 zkgui）对 WiFi 凭据与部署方式的现有假设。

## 0. 结论速览

| # | 问题 | 结论 | 置信度 |
|---|---|---|---|
| 1 | `/data/setting.ini` 里 WiFi 键名 | **确实是明文 INI，`[main]` 段下 `wifiSsid=` / `wifiPwd=`**（camelCase，精确匹配） | 确认（键名/段）；高概率（值明文） |
| 2 | `base::wifiOnAndWait()` 之后是否已联网 | **否**。它只负责"把射频打开并等 enabled"，不联网、不等 IP | 确认 |
| 2 | 系统 wpa_supplicant 是否自动连上次网络 | **是**。`/data/misc/wifi/wpa_supplicant.conf` 里存 `network={ssid,psk}` 且模板 `update_config=1` | 高概率 |
| 2 | `getConnectionInfo()` 是否含密码 | 类型上**有** `WifiInfo::getPasswd()`；开机自动连的场景下是否被填充 | 确认（API 存在）/ 未知（是否非空） |
| 3 | ADB"下载调试"是否必须 TF 卡/U 盘 | **不必须**。内置 `/tmp` 目标；Z21 无卡也能下 | 确认 |
| 3 | 推送布局 | `<目标目录>/{EasyUI.cfg,ui/,lib/,bin/,font/,tr/}`，文件 0664、`bin/` 0764 | 确认 |
| 3 | 重启命令 | `sync` → `setprop ctl.restart zkswe` | 确认 |
| 4 | `update.img` 怎么来 | IDE 用 **jar 内置**的 `zkswe_mkimg.exe`(做 squashfs) + `fsimg.exe`(打包) | 确认 |
| 4 | 安装包里是否有这两个 exe | **有，但在 jar 内**（`bundle/bin/…`），磁盘上不存在独立文件 | 确认 |
| 4 | 是否只能 TF 卡升级 | **否**。另有官方 ADB 固化（`sys.zkupgrade.*`）、远程 HTTP、局域网批量升级 | 确认 |
| 4 | 有没有官方"直接把文件固化到 `/data`、`/usr`"的做法 | **没有**；且 `adb push` 到 `/data`(JFFS2) 会卡死 USB | 确认 |
| 5 | 社区/官方资料是否支持"WiFi ADB + 镜像编译 + TF 卡" | 是，官方 SDK README 与同类 App README 均这么写 | 确认 |

**一句话结论**：我们现有假设"WiFi 凭据可从 `/data/setting.ini` 的 `[main] wifiSsid/wifiPwd` 读取"**是对的**，但更稳的第一来源其实是 `/data/misc/wifi/wpa_supplicant.conf`（官方 `base::parseWpaSsidAndPsk()` 就是干这个的）；而"`wifiOnAndWait()` 之后就已经连上"这个隐含假设**是错的**，必须自己等/自己连。

---

## 1. 官方固件 `/data/setting.ini` 的真实格式与 WiFi 键名

### 1.1 结论

- `/data/setting.ini` 是**厂商 App 自己的设置文件**（不是 FlyThings 系统文件），扁平 `key=value` 的 INI，带 `[main]` 段。
- WiFi 键名**就是 `wifiSsid` 和 `wifiPwd`**，位于 `[main]` 段，值与其它字段同级、**无引号、无编码标记**。
- 同文件还存云端凭据：`secretKey` / `authToken` / `authRefreshToken` / `tokenExpireTime` / `refreshTokenExpireTime`，以及 `macAddress` / `deviceSn` / `volume` / `toolsOrder`。

### 1.2 证据

**证据 1-1（最硬）：第三方生产代码按精确键名剥离该文件里的凭据**

`https://github.com/Blueforcer/awtrix-ng/blob/main/docs/assets/tc002/install/install.js`，本地副本 `C:\Users\jason\AppData\Local\Temp\awtrix_install.js`：

- L10–L11 定义"必须抹掉的厂商机密键"：
  ```
  const secrets = new Set(['secretKey', 'authToken', 'authRefreshToken', 'tokenExpireTime',
    'refreshTokenExpireTime', 'wifiSsid', 'wifiPwd']);
  ```
- L218 指定文件：`const settings = '/data/setting.ini', supplicant = '/data/misc/wifi/wpa_supplicant.conf';`
- L37–L56 `inspectVendorSettings()` 的解析规则：按 `\n`/`\r\n` 切行、**去 UTF-8 BOM**、`trim`；`[`/`;`/`#` 开头的行不当键；键 = 第一个 `=` 之前的内容；`secrets.has(key)` 命中则**整行删除**，否则原样保留。
- L265–L268 写完再读回校验，若仍能剥离出凭据就**直接报错中止**（`still holds vendor credentials`）——说明这是跑在真机上、必须正确的生产逻辑。

同仓库 `tools/tc002/install/tc002_install.py` L106–L111（本地 `C:\Users\jason\AppData\Local\Temp\awtrix_tc002_install.py`）：
```
VENDOR_SETTINGS   = "/data/setting.ini"
VENDOR_SUPPLICANT = "/data/misc/wifi/wpa_supplicant.conf"
VENDOR_HOSTAPD    = "/data/misc/wifi/hostapd.conf"
SUPPLICANT_TEMPLATE = "/etc/wifi/wpa_supplicant.conf"
VENDOR_OFFLINE = {"persist.wifi.on": "0", "persist.softap.on": "0"}
```

**证据 1-2：该项目的测试夹具给出了文件的完整形态**

`tools/tc002/install/test_tc002_install.py` L66–L71（本地 `C:\Users\jason\AppData\Local\Temp\awtrix_test_install.py`）：

```
SETTINGS = b"[main]\nmacAddress=AABBCCDDEEFF\ndeviceSn=SN-TEST-0001\nsecretKey=k-secret-1\n"
           b"authToken=t-secret-2\nauthRefreshToken=r-secret-3\ntokenExpireTime=1790000000\n"
           b"refreshTokenExpireTime=1790600000\nwifiSsid=HomeNet\nwifiPwd=p-secret-4\nvolume=5\n"
           b"toolsOrder=1,2,3"
CLEAN_SETTINGS = b"[main]\nmacAddress=AABBCCDDEEFF\ndeviceSn=SN-TEST-0001\nvolume=5\n"
                 b"toolsOrder=1,2,3"
```
即清洗后 `wifiSsid`/`wifiPwd`/`secretKey`/`authToken`/两个 `*ExpireTime` 消失，`macAddress`/`deviceSn`/`volume`/`toolsOrder` 保留。

**证据 1-3：官方协议文档印证该文件是"所有设置"的落盘处**

`C:\Users\jason\AppData\Local\Temp\uclock-sdk\protocol\Ulanzi_TC002_protocol-http-mqtt_CN.md` L440：
> `POST /resetConfig`: no body. Deletes `/data/setting.ini`, clears `/data/diy`, then reboots; all config returns to defaults.

另有 `POST /setWifiConfig`（L269–L276）入参 `{"ssid","password"}`，异步受理——说明 WiFi 配置由 App 层收下再落盘/下发。

**证据 1-4：`setting.ini` 不是 FlyThings 系统文件**

在 `D:\zkswe\FlyThingsIDE\bin\.dep`（全部依赖包头文件+静态/动态库）与 IDE 的 `sdk` 目录做二进制搜索：
```
findstr /S /M /C:"setting.ini" "D:\zkswe\FlyThingsIDE\bin\.dep\*" "D:\zkswe\FlyThingsIDE\sdk\*"
→ 无任何命中
findstr /S /M /C:"wpa_supplicant" 同上
→ base-utility\10.9.3\include\base\wifi.h
   base-utility\10.9.3\lib\libbase-utility.a
   zknet\0.0.0\include\net\WifiCtrl.h
   zknet\0.0.0\include\net\WifiManager.h
   zknet\0.0.0\lib\libzknet.so
```
→ SDK 只认 `wpa_supplicant`，从不认 `setting.ini`。所以 `setting.ini` 的 schema 完全由 Ulanzi 自家 App 决定。

### 1.3 仍未确认的部分（明确标"未知"）

- **真机字节级原文未知**：手头没有 TC002 固件/真机 dump（全盘搜索 `D:\Project\ulanzi`、`uclock-sdk` 无 `update.img` / `setting.ini` / `*.ini`，见 §附录 A）。上面格式来自生产代码 + 测试夹具，而非真机抓取。
- **`wifiPwd` 是否加密/编码：高概率明文，未 100% 确认**。理由：夹具里就是裸字符串 `p-secret-4`；剥离代码从不解码、只按"凭据"整行删；同文件 `authToken`/`secretKey` 亦为裸值。但若 Ulanzi 对 `wifiPwd` 做了 base64/异或，剥离逻辑同样成立，所以不能算"确认"。
- **是否还有 `[wifi]`/`[wlan]` 等其它段**：未知。剥离逻辑**不区段**（键匹配是全文件的），夹具只有 `[main]`。所以读取时按"全文件找键、优先 `[main]`"最稳。
- **值里是否可能带 `=`**：无证据。解析时只按第一个 `=` 切键，值原样保留即可（我们读时应 `trim`，不要按第二个 `=` 切）。

**对我们的直接含义**：`WifiStore.cpp` 现有 `keyIsSsid()`/`keyIsPassword()` 的多键名猜测（`wifi`/`name`/`key`/`psk`…）属于过度兜底且**有误命中风险**（例如把 `secretKey` 当密码没有，但 `key` 这种太宽）；`wifiSsid`/`wifiPwd` 才是精确主键。

---

## 2. TC002 开机 WiFi 行为

### 2.1 谁在管 WiFi（确认）

`D:\zkswe\FlyThingsIDE\bin\.dep\public\Z21\zknet\0.0.0\lib\libzknet.so` 里的可打印字符串（用 pwsh 读字节流提取 ≥5 字符的 ASCII 段）：

```
/data/misc/wifi/wpa_supplicant.conf
/etc/wifi/wpa_supplicant.conf
mkdir -p /data/misc/wifi/
cp -ZkKz /etc/wifi/wpa_supplicant.conf /data/misc/wifi/wpa_supplicant.conf
cp /etc/wifi/wpa_supplicant.conf /data/misc/wifi/wpa_supplicant.conf
/data/misc/wifi/sockets
/data/misc/wifi/sockets/wpa_ctrl_%d-%d
init.svc.wpa_supplicant
persist.wifi.on
persist.softap.on
persist.sys.softap.ssid
persist.sys.softap.pwd
ADD_NETWORK / SET_NETWORK %d / LIST_NETWORKS / GET_NETWORK %d / ENABLE_NETWORK %d
wpa2-psk / wpa-psk / wpa_psk=%s
[EventThread] wpa_supplicant is not running, break
[EventThread] Wrong PASSWORD do disconnect  !!!
```

即：WiFi 由 wpa_supplicant 守护（init 服务名 `init.svc.wpa_supplicant`）管理，配置在 `/data/misc/wifi/wpa_supplicant.conf`，首次若缺失就从 `/etc/wifi/wpa_supplicant.conf` 模板拷贝；`WifiCtrl` 通过 `wpa_ctrl` 控制接口（socket 在 `/data/misc/wifi/sockets/`）下发 `SET_NETWORK`。开关机由属性 `persist.wifi.on` 控制，热点由 `persist.softap.on` + `persist.sys.softap.ssid/pwd` 控制（**热点名/密码就是明文属性**）。

### 2.2 系统是否会自动连上次网络（高概率：会）

`config/tc002/install/test_tc002_install.py` L72–L73 给出 supplicant 配置形态：
```
TEMPLATE  = b"ctrl_interface=/dev/socket\nupdate_config=1\n"
SUPPLICANT = TEMPLATE + b'\nnetwork={\n\tssid="HomeNet"\n\tpsk="p-secret-4"\n}\n'
```
- `update_config=1` → wpa_supplicant **自己会把连过的网络写回该文件**；
- `network={ ssid / psk }` 里的 **psk 是明文**；
- 有 enabled 的 network block 时，wpa_supplicant 启动即自动关联——这是标准行为。旁证：awtrix-ng 必须**先把该文件换回无 network block 的模板**才算"清掉联网能力"，并在文档里说这样做是因为"stock `wpa_supplicant` … could write credentials back from memory"（[install-tools](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/install-tools/)）。

配合 `base::wifiOnAndWait()` 之后系统侧的自动关联，**我们"等系统自动连"的策略是成立的**。

### 2.3 `base::wifiOnAndWait()` 到底做了什么（确认：只开射频，不联网）

`D:\zkswe\FlyThingsIDE\bin\.dep\public\Z21\base-utility\10.9.3\include\base\wifi.h` L17–L24：
```cpp
/** @brief 等待WIFI完全开启  @param timeout 超时时间 单位秒
 *  @return true 成功开启 */
bool wifiOnAndWait(int timeout);
void wifiOffAndWait();
```
`libbase-utility.a` 的符号与日志字符串进一步限定语义：
```
_ZN4base13wifiOnAndWaitEi          base::wifiOnAndWait(int)
_ZN4base14wifiOffAndWaitEv
_ZN4base18parseWpaSsidAndPskE...   base::parseWpaSsidAndPsk(std::string*, std::string*)
_ZN4base18WPA_CONF_FILE_PATHE      WPA_CONF_FILE_PATH
_ZN4base4L13wifiAwaitTrueEPNS_13WIFIConnecterE...   (内部) wifiAwaitTrue(conn, predicate, ...)
_ZN4base4L11connectWifiEPNS_13WIFIConnecterE...     (内部) connectWifi(conn, ssid, pwd)
toggleWifi
"[%s:%d %s] await wifi enabled"
"[%s:%d %s] system not support wifi"
"[%s:%d %s] waiting wifi %s"
"[%s:%d %s] connecting over 3 minutes, restart wifi"   ← 属 connectWifi，不属 wifiOnAndWait
"[%s:%d %s] wifi connect success"
"/data/misc/wifi/wpa_supplicant.conf"
```
→ `wifiOnAndWait` 走的是 `toggleWifi` + `wifiAwaitTrue(…"await wifi enabled")`；真正"连接"是另一条 `connectWifi`（由 `WIFIConnecter::doTask` 使用）。所以：

- **`wifiOnAndWait()` 返回 true ≠ 已联网，也 ≠ 已拿到 IP。**
- 现有 `WifiManager_task.cpp` 里"`wifiOnAndWait(15)` 之后进 `waitForConnection(kAutoConnectWaitSec)`"的结构是正确方向（L234–L255），需保证 `waitForConnection` 给足时间且判据包含 IP。

### 2.4 `getConnectionInfo()` 是否含密码

- `net\WifiManager.h` L307：`WifiInfo* getConnectionInfo() const;`
- `net\WifiInfo.h` L19–L25：`const std::string& getPasswd() const;` / `setPasswd(...)`，私有成员 `mPasswd`。

→ **API 层面"有密码字段"= 确认**。
- 但 `WifiManager` 内部有两个对象：`mRequestConnectInfo`（`connect(ssid,pw)` 时填）与 `mConnectionInfo`（`getConnectionInfo()` 返回的那一个，L400–L401）。`mConnectionInfo` 的 `mPasswd` 是否被填、开机自动连（我们没调用过 `connect()`）时是否为空 —— **未知**，无任何证据（头文件不体现实现，二进制里也没有可判定的字符串）。
- **结论：不要在开机自动连的场景依赖 `getConnectionInfo()->getPasswd()`。** 要密码请用官方提供的
  `base::parseWpaSsidAndPsk(&ssid, &psk)`——`wifi.h` L39–L45 明确写它"从 `/data/misc/wifi/wpa_supplicant.conf` 中读取 wifi 名称及密码"，且 `libbase-utility.a` 里确有 `WPA_CONF_FILE_PATH` + 该路径字符串（确认）。
- 另外 `wifiRssi()`（未连接返回 0）可作极轻量的在线判据（`wifi.h` L26–L30）。

### 2.5 现存代码里的对照点

`native\MultiTimer\src\net\WifiManager_task.cpp` L244 注释"情况 A：系统 wpa_supplicant 已经保存过网络，上电会自动连"——与本节结论**一致**；L211–L214 只取 `getSsid()`（不取密码），这个用法**是安全的**。

---

## 3. FlyThings IDE 的 ADB"下载调试"实际推送路径 / 启动命令

> 方法：`com.zkswe.ide.editor_2.0.0.202608170922.jar` **自带 `src/**.java` 源码**（未混淆），直接解出读源码，并用 `javap` 交叉验证常量。

### 3.1 连接方式（确认）

`com/zksw/editor/handler/LaunchOverAdb.java` `launch2()` L492–L505：
- WiFi：`conn.connectToTcpDevice(new InetSocketAddress(ip, 5555))` —— **端口 5555**（`FlyThingsPreference.isUseWifiConnectionAdb()` 为真时）。
- USB：仅在非 WiFi 模式；若存在 offline 设备则重启 adb server。
- 平台校验：L519–L531 读 `ro.product.model`，与工程平台比对；不一致直接抛 `平台不一致`。
  `com/zksw/editor/app/config/FlyThings.java` L157–L176 映射表：
  `{ "Zkswe_SSD21X_SPINOR", PLATFORM_ID_Z21, "Z21" }` → **TC002 的 `ro.product.model` 是 `Zkswe_SSD21X_SPINOR`，对应 Z21**。
  （旁证：awtrix-ng `install.js` L4 `const MODEL = 'Zkswe_SSD21X_SPINOR';`，`tc002_install.py check_model()` 同样校验 `ro.product.model`。）

### 3.2 是否必须 TF 卡 / U 盘（确认：不必须）

`FlyThingsPreference.java` L39–L42：
```java
public final static String ATTR_ADB_DOWNLOAD_PATH     = "adb_download_path";
public final static String ADB_DOWNLOAD_PATH_MNT_EXTSD = "/mnt/extsd";
public final static String ADB_DOWNLOAD_PATH_MNT_USB1  = "/mnt/usb1";
public final static String ADB_DOWNLOAD_PATH_TMP       = "/tmp";     // ← 内置 /tmp 目标
// 默认值 L131: store.setDefault(ATTR_ADB_DOWNLOAD_PATH, "/mnt/extsd");
```
`launch2()` 的硬性前置只有两处：
- L533–L538：仅当平台是 `A33EMMC` 才要求 `/mnt/extsd` 已挂载（Z21 不触发）。
- L540–L544：仅当目标目录**正好等于** `/mnt/usb1` 才要求 U 盘挂载。

→ **Z21（TC002）用 `/tmp` 或默认 `/mnt/extsd` 都可以"无卡"下载调试**；把目标设为 `/mnt/usb1` 才需要 U 盘。
另外 `/tmp` 是 tmpfs（设备内存），空间不足时报 `No space left on device` 并追加提示"设备内存可能已耗尽"（L443–L449）。

### 3.3 推送内容与布局（确认）

`packProcess()` 在**项目内** `<project>/imgout` 组装（L116 `mTemp = mProject.getFolder("imgout")`，跑完删除 L152–L159），目录常量 L183–L187：
```java
SD_UI_FOLDER = "ui"; SD_LIB_FOLDER = "lib"; SD_BIN_FOLDER = "bin";
SD_FONT_FOLDER = "font"; SD_TRANSLATION_FOLDER = "tr";
```
内容来源：
| 目标 | 来源 | 证据 |
|---|---|---|
| `EasyUI.cfg` | 由工程配置生成（**仅当 `partition=="res"`**） | L363–L385 |
| `ui/` | 工程 `ui/` 下所有 `*.ftu`；**外加** `resources/` 整个目录推到 `<目标>/ui` | L249–L268、L550–L555 |
| `lib/` | 编译产物 `lib<app>.so` + `dependencies/`、`dependencies/lib`、`dependencies/lib-no-link` 下所有 `lib*.so*` | L387–L420 |
| `bin/` | `dependencies/bin` 下所有文件（**权限 0764**） | L556–L558 |
| `font/` | 可选：`EXTRA_ENABLE_FONT_LOCATION=true` 时工程 `font/*.ttf` | L309–L333 |
| `tr/` | 工程 `i18n/*.tr` 导出为每语言一个 `<lang>.json` | L245–L247、L696+ |

推送路径由 `makeEntries()` L788–L812 决定：`remotePath = remoteDirectory + "/" + 相对路径`，**保留子目录结构**，普通文件 mode `0664`。`checkDirectory()` L814–L819 禁止目标目录以 `/` 结尾。

`EasyUI.cfg` 是 JSON，字段即 `com/zksw/editor/app/config/Config.java` 的属性（L10–L49）：
`resPath / startupLibPath / languagePath / font / touchDev / rotateTouch / rotateScreen / uart / baud / zkdebug / screensaverTimeOut / defBrightness / startupTouchCalib / dictPinyinPath / languageCode`。
其中调试模式被强制 `zkdebug=true`（L286），且路径按目标目录重写（L355–L361）：
```java
setResPath(adbDownloadPath + "/ui/");
setStartupLibPath(adbDownloadPath + "/lib/" + so.getName());
setLanguagePath(adbDownloadPath + "/tr/");
setTouchDev(Z21 ? "/dev/input/event0" : "/dev/input/event1");   // L335–L345
```
→ **TC002/Z21 触摸节点 = `/dev/input/event0`**。

### 3.4 重启/启动命令（确认）

`launch2()` L584–L588：
```java
Execute[] tasks = new Execute[] {
    new Execute("sync", "sync"),
    new Execute("restart", "setprop", "ctl.restart", "zkswe"),
};
Jadb.batchExecute(device, tasks, monitor, "重新启动程序");
```
即推送完成后执行 **`sync`** 再 **`setprop ctl.restart zkswe`**（重启 GUI 服务，等于重启我们的 App）。
该命令在三处独立互证：`IDE使用说明/说明文档.md` §查看标准输出日志（`adb shell setprop ctl.stop zkswe` / `zkgui`）、§通过 ADB 固化；以及 awtrix-ng `status()` 里读 `init.svc.zkswe`。

**注意（容易踩的坑）**：`LaunchOverAdb` 里另有一套 `getCommandList()`（L653–L664，`adb root` + `adb push` + `adb shell sync` + `setprop`）和 `connectDeviceOverWifi()`（L674–L687），但**二者与 `qualifiedExcuted()` 均无调用点**（全文件 grep 只有定义处）——是死代码。**现行路径是纯 Java 的 Jadb sync 协议**（`SyncTransportSession`），不是外部 `adb push`。所以不要指望 IDE 会执行 `adb root`。

---

## 4. 持久化安装（固化）

### 4.1 `update.img` 如何生成（确认）

`com/zksw/editor/handler/PackFlyThings.java`：
- 清空 `imgout` → 组装 → 生成 `res.tmp` → 生成 `update.img`（L167–L173、L586+）。
- 默认文件系统：Z21 → `squashfs`（L600–L610，仅 `*EMMC` 平台才 `ext4`）。
- squashfs 由 **`zkswe_mkimg.exe`** 产生（L624–L631）：
  ```
  bundle/bin/zkswe_mkimg.exe  imgout res.tmp -noappend -force-uid 1000 -force-gid 1000 -comp xz
  ```
- 打包由 **`fsimg.exe`** 完成（L638–L645）：
  ```
  bundle/bin/fsimg.exe -i res:res.tmp -o update.img -p <platformHex>
  ```
  **Z21 的 platformHex = `0x000606`**（L697–L698，`getPlatformHex()`）。
- 成功后移动到"镜像输出目录"（L660–L662）；失败会提示"确认 IDE 是否安装完整 / 加杀毒白名单"（L653–L655）。

### 4.2 当前安装里是否有这两个 exe（确认：有，但在 jar 内）

磁盘上**不存在**：
```
Get-ChildItem -Recurse -File 'D:\zkswe\FlyThingsIDE' -Include 'zkswe_mkimg.exe','fsimg.exe','mkimg*','fsimg*'
→ 无结果
```
它们作为**资源条目**在 editor jar 里（`System.IO.Compression.ZipFile` 列目录）：
```
bundle/bin/zkswe_mkimg.exe
bundle/bin/fsimg.exe
bundle/baseimg/update.img      ← "基础系统包"模板
bundle/font/fzcircle.ttf
```
运行时由 `Util.getBundleEntry("bundle/bin/zkswe_mkimg.exe")` 解出到临时位置再执行（`PackFlyThings.java` L628/L643、L745 `baseVersionImageFile()` → `bundle/baseimg/update.img`）。
→ 所以：**"镜像编译"不需要你另外准备 `bundle/bin`**，IDE 自带；但它不是可直接调用的命令行工具，而是 IDE 内部资源。
（附带观察：`baseVersionImageFile()` 的返回值在 L591 只做了 `== null` 判空、**并未传给 mkimg/fsimg 命令行**，该基础包更可能是用作完整性/版本基线的素材，实际是否参与打包无法从头文件级证据确定——标 **未知**。）

### 4.3 是否只能 TF 卡升级（确认：不是，至少 4 条路）

| 方式 | 做法 | 是否需 TF 卡 | 证据 |
|---|---|---|---|
| TF 卡镜像 | `update.img` 放 FAT32 卡根目录 → 上电 → 弹升级界面勾选升级 | 需要 | `IDE使用说明/说明文档.md` §制作升级镜像 / §从TF卡启动程序 |
| **ADB 固化（官方）** | `adb push ./update.img /tmp/update.img` → `setprop sys.zkupgrade.flag 255` → `setprop sys.zkupgrade.dir /tmp` → `setprop ctl.restart zkswe` | **不需要** | 同文档 §通过 ADB 固化应用程序 |
| 自动升级 | TF 卡根目录建无后缀文件 `zkautoupgrade`（内容=延时秒数），可选 `zkrebootdelay` | 需要 | 同文档 §自动升级 |
| 远程升级 | App 内 `UpgradeMonitor::checkUpgradeFile("/mnt/extsd/temp")`；HTTP `POST /update {downloadUrl,md5,size}` | 不需要 | 同文档 §远程升级；`protocol/…CN.md` L428–L435 |
| 局域网批量 | 多台按 IP 升级（仅 Z20/Z21/Z261） | 不需要 | 同文档 §批量升级 |

### 4.4 有没有"ADB 直接固化到 `/data`、`/usr`"的官方做法（确认：没有，且不要试）

- 官方唯一的 ADB 固化路径就是上表第 2 行：**push 到 `/tmp` → 设 `sys.zkupgrade.*` → 让升级守护进程写分区**。它并不"直接写 /data 或 /usr"。
- 社区侧明确警告：**`adb push` 进 JFFS2 会卡死 USB**，所以所有工具都只经 `/tmp` 暂存。原文（[install-tools](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/install-tools/) "Traps"）："`adb push` into JFFS2 wedges USB: every tool stages through `/tmp`"；同页 "Flash method"：adb writes only to `/tmp` (tmpfs), never to `/data`。
- 分区事实（同页 "Flash and space"）：`res` = mtd3 / 8 MiB / squashfs(xz)，**官方 App 与 `/etc/EasyUI.cfg` 都在这里**；`data` = mtd6 / 8 MiB / JFFS2；`/tmp` = tmpfs。
  → 也就是说"替换 zkgui"本质是**改 `res` 分区**，这正好解释为什么必须走 `update.img`。

### 4.5 恢复官方固件的复位键流程（确认）

- 官方 SDK：`Z21_TC002_Demo/README.md` L120
  > **主动刷机**：如果想要重刷为官方固件，请按下设备上的重置按钮（位于 USB-C 旁边）并保持按住，会自动刷回官方固件。
- 社区侧更具体（[install-tools](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/install-tools/) "Reset button"）：
  > Holding the reset button for 5 s restores the Ulanzi firmware and wipes `/data`, AWTRIX NG and everything in `/data` included. The restored vendor app can be older than the one the clock had.
- 含义：**长按约 5 秒 = 恢复官方固件 + 清空 `/data`**。对我们意味着：`/data` 里的 `setting.ini`、我们自己的 `multitimer.*` 都会一起消失；这是最后的安全网，但也是"会把用户数据清空"的操作。

### 4.6 部署形态的两个硬约束（会影响我们）

1. **stock 固件的 USB ADB 只有上电后约 2~4.5 秒窗口**，随后内核 USB 扫描把 OTG 切成 host 模式，USB ADB 消失（[install-tools](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/install-tools/) "USB keeper"；同页 "Connect" 说 "about 2 s to 4.5 s"）。这与 IDE 文档"带 Wi-Fi 的机型 USB 连接无效"完全吻合 → **日常迭代只能走 WiFi ADB（5555）**。
2. **防砖回滚是强制的**：`Z21_TC002_Demo/README.md` L102–L107
   > **防砖检测**：系统启动后必须在入口处设置运行标志，否则系统将触发防砖回滚。
   > `SystemProperties::setString("sys.zkapp.state", "running");`
   我们的 `native/MultiTimer/src/Main.cpp` L13 已设置该属性（已核对），**固化后务必保留**，否则新固件会被回滚。

---

## 5. 官方 SDK / 社区资料核对

| 资料 | 关键内容 | 置信度 |
|---|---|---|
| `uclock-sdk/README_CN.md` L274–L281 | `base::wifiOnAndWait(10); // 必须先开 Wi-Fi`；可选把 WiFi/BLE 预置到 U 盘 `/mnt/usb1/test.cfg` | 确认 |
| `uclock-sdk/Z21_TC002_Demo/README.md` L54–L86 | `#include <base/wifi.h>`；`base::wifiOnAndWait(10)`；`test.cfg` 字段 `ssid/pwd/isConnect/ble`；`ble` 空则用 WiFi MAC 后四位生成 `Ulanzi TC002 AB12`；**BLE 必须在 WiFi 已开启前提下** | 确认 |
| `Z21_TC002_Demo/src/logic/mainLogic.cc` L18–L59 | 实际实现：先 `wifiOnAndWait(10)`，再读 `/mnt/usb1/test.cfg` 的 `ssid/pwd/isConnect/ble`，然后 `BluetoothService::start()`，把 `ssid/pwd` 交给 `WifiTestPage` | 确认 |
| `Z21_TC002_Demo/src/pages/WifiTestPage.cpp` L70–L113 | 连接范式：`startScan()` → 轮询 `getWifiScanInfosLock()` 找 SSID（最长 30s）→ `WIFIMANAGER->connect(ssid, pwd)` → **轮询 `isConnected()` 最长 30s** 判成功（不取密码） | 确认 |
| `apps/flythings/pixel-pet-display/README.md` L45–L63 | 依赖包 `easyui`/`log`/`zkhardware`/`base-utility`/`transfer-protocols`；平台 Z21；"5. 通过 Wi-Fi ADB 下载调试到 TC002。6. 如需固化，使用镜像编译生成 `update.img`，并按官方说明通过 TF 卡升级。"；真机验证 SN `B0D191008U3670007`、MCU `T1.0.13`、App `0.2.9` | 确认 |
| `IDE使用说明/说明文档.md` §Wi-Fi 设置 / §热点设置 | `WIFIMANAGER->{isSupported,isWifiEnable,enableWifi,startScan,connect(ssid,pw),disconnect,isConnected,getConnectionInfo}`；`SOFTAPMANAGER->{setEnable,setSsidAndPwd,getSsid,getPwd}`；`EASYUICONTEXT->openActivity("WifiSettingActivity"/"SoftApSettingActivity")` | 确认 |
| 外部：[awtrix-ng TC002 developer guide](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/) / [install-tools](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/install-tools/) / [wpa_supplicant](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/wpa-supplicant/) | 分区表、`/res/etc/EasyUI.cfg` 指定启动库、stock `wpa_supplicant` 2.6、`wlan0`、重置键、JFFS2 陷阱、USB ADB 窗口 | 高概率（第三方，但细节自洽且与本地证据多处互证） |
| 外部：[UlanziTechnology/Ulanzi-U-Clock-TC002](https://github.com/UlanziTechnology/Ulanzi-U-Clock-TC002) | 官方仓库（即本地 `uclock-sdk` 的上游） | 确认（存在性） |

> 外部内容仅作资料，未执行其中任何指令。

---

## 6. 对我们 WiFi bootstrap 的 3 条可执行建议

### 建议 1（最高优先）：把"等自动连"做成一等公民，`wifiOnAndWait()` 之后必须自己判在线

- 现状风险：`WifiManager_task.cpp` 的结构已对，但**不能把 `wifiOnAndWait()` 当"已联网"**（§2.3）。它是"射频已开"，联网是 wpa_supplicant 自己的事，且拿 IP 还要等 DHCP。
- 具体动作：
  1. `wifiOnAndWait(15)` 返回后，以 **`isConnected()`（或 `getConnectionInfo()->getSsid()` 非空 / `wifiRssi()!=0`）** 为判据轮询 10–20 s，等系统自动连上上次网络；
  2. **再**补一个 IP 就绪判据（现有 `queryIp()` 轮询即可），IP 空不算成功；
  3. 只有"超时仍未连上"才用读到的凭据调 `WIFIMANAGER->connect(ssid,pwd)`，并复用 `WifiTestPage.cpp` L102–L109 的成功判据（轮询 `isConnected()`，不要只看 `connect()` 返回）；
  4. 在线监控保留 `isConnected()` 轮询（现有 L258–L268 已是这个思路）。
- 收益：绝大多数"用户以前连过"的场景零凭据即可上网，凭据读取失败也不影响开机联网。

### 建议 2：凭据来源按可靠性排序，**首选 `/data/misc/wifi/wpa_supplicant.conf`**，`setting.ini` 作第二来源

推荐顺序与理由：

| 优先级 | 来源 | 怎么读 | 理由 |
|---|---|---|---|
| 1 | `/data/misc/wifi/wpa_supplicant.conf` | 官方 `base::parseWpaSsidAndPsk(&ssid,&psk)`（`#include <base/wifi.h>`） | 这是**系统真正在用**的那份；`psk="明文"`（§2.2）；官方专门提供了读取 API（§2.4）；自动连得上就一定能读到 |
| 2 | `/data/setting.ini` 的 `[main] wifiSsid/wifiPwd` | 现有 INI 解析，但**主键收敛到这两个精确键名** | 键名/段已确认（§1）；但注意**我们替换 zkgui 后没人再更新它**，会随时间变旧（只在官方 App 最后一次配置时有效） |
| 3 | `/mnt/usb1/test.cfg` | JSON `ssid/pwd/isConnect/ble`（官方 SDK 示例同一路径） | 官方认可的"U 盘配网"兜底；适合产线/无 WiFi 时人工投放 |
| 4 | `/mnt/extsd/test.cfg`、`/data/multitimer.wifi` | 现有实现 | 自建兜底/用户覆盖，保留但降级 |

配套调整（只描述，不在本次改代码）：
- `WifiStore.cpp` 的 `keyIsSsid()/keyIsPassword()` 建议**收紧**：以 `wifiSsid/wifiPwd` 为精确主键，最多保留 `ssid/pwd/password/psk` 这 4 个通用兜底；删掉 `key`/`name`/`wifi` 这类易误命中的宽匹配。
- **空密码（开放网络）要能走到连接**：`WifiCredential::valid()` 是 `!ssid.empty()`（已核对 `WifiStore.h` L17），所以"读"没问题；风险在"用"——`WifiManager::connect(ssid, "")` 必须走开放网络分支（`libzknet.so` 里有 `SET_NETWORK %d key_mgmt NONE`，能力具备），不要把空 `wifiPwd` 当成"读取失败"而跳过该来源。
- INI 解析保持"**不区段**地在全文件找键、优先 `[main]`"（§1.3），值按第一个 `=` 切、`trim`，并保留 BOM 处理（现有实现已有）。

### 建议 3：部署走"`/tmp` WiFi ADB 迭代 + `update.img` 固化"，SoftAP 配网列为可选兜底

- **日常迭代**：WiFi ADB（设备 IP:5555）+ 下载目录设 `/tmp`，推送布局照 §3.3（`EasyUI.cfg` + `ui/` + `lib/` + `bin/`），重启 `sync; setprop ctl.restart zkswe`。**不需要 TF 卡**（§3.2）。USB 只在上电后 2–4.5 s 可用，别依赖（§4.6）。
- **固化**：`update.img`（IDE 镜像编译，Z21 平台码 `0x000606`）→ 优先 **TF 卡 FAT32 根目录**（最稳，有升级界面可勾选）；需要远程/批量时用 **官方 ADB 固化**：`adb push ./update.img /tmp/update.img` → `setprop sys.zkupgrade.flag 255` → `setprop sys.zkupgrade.dir /tmp` → `setprop ctl.restart zkswe`。
  **禁止** `adb push` 到 `/data`（JFFS2，会卡死 USB，§4.4）。
- **必查项**：固化前确认 `Main.cpp` 的 `SystemProperties::setString("sys.zkapp.state","running")` 仍在（防砖回滚，§4.6）；工程 `Manifest.xml` 平台必须是 **Z21**（设备 `ro.product.model` = `Zkswe_SSD21X_SPINOR`，否则下载被平台校验拒绝，§3.1）。
- **SoftAP 配网：做，但排最后**。可行性没问题（`SOFTAPMANAGER->setSsidAndPwd()/setEnable()`，底层是 `persist.softap.on` + `persist.sys.softap.ssid/pwd`，§2.1）；但它是**新功能**（要 UI/状态机/超时回退），而建议 1+2 已能覆盖"曾经连过"和"U 盘投放"两大场景。建议先把 1、2 做扎实，把 SoftAP 作为"三个来源都拿不到凭据"时的 case C，并设明确超时（例如 180 s 无配网自动关热点、继续重试自动连）。

---

## 附录 A：证据索引与复现命令

### A.1 本地文件（均已读取，未修改）

| 路径 | 用途 |
|---|---|
| `D:\Project\ulanzi\native\MultiTimer\src\config\WifiStore.cpp` / `.h` | 现有凭据读取实现（第 2 节末、建议 2 对照） |
| `D:\Project\ulanzi\native\MultiTimer\src\net\WifiManager_task.cpp` | 现有 bootstrap 流程（L200–L291） |
| `D:\Project\ulanzi\native\MultiTimer\src\Main.cpp` | 已设 `sys.zkapp.state`（L13） |
| `D:\zkswe\FlyThingsIDE\bin\.dep\public\Z21\base-utility\10.9.3\include\base\wifi.h` | `wifiOnAndWait` / `parseWpaSsidAndPsk` 声明 |
| `…\base-utility\10.9.3\lib\libbase-utility.a` | 上述符号与日志字符串（§2.3） |
| `…\zknet\0.0.0\include\net\WifiManager.h`、`WifiInfo.h`、`WifiCtrl.h` | `getConnectionInfo` / `getPasswd` / wpa_ctrl |
| `…\zknet\0.0.0\lib\libzknet.so` | wpa_supplicant 路径、属性、wpa_ctrl 命令（§2.1） |
| `C:\Users\jason\AppData\Local\Temp\uclock-sdk\**` | 官方 SDK 浅克隆（README / protocol / Z21_TC002_Demo / pixel-pet-display） |
| `C:\Users\jason\AppData\Local\Temp\zkswe_src\src\**` | 从 editor jar 解出的 Java 源码 |
| `C:\Users\jason\AppData\Local\Temp\awtrix_install.js`、`awtrix_test_install.py`、`awtrix_tc002_install.py`、`awtrix_layout.h` | 第三方 TC002 安装器与其测试夹具（§1、§2.2、§4.4） |

### A.2 复现命令

```powershell
# 列出 editor jar 内的源码与 bundle 资源（确认 zkswe_mkimg.exe / fsimg.exe / baseimg 在 jar 内）
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z=[System.IO.Compression.ZipFile]::OpenRead('D:\zkswe\FlyThingsIDE\bin\plugins\com.zkswe.ide.editor_2.0.0.202608170922.jar')
$z.Entries | ? { $_.FullName -match 'bundle/|LaunchOverAdb|PackFlyThings|DebugConfig' } | % FullName
$z.Dispose()

# 解出 Java 源码（未混淆）
#   → src/com/zksw/editor/handler/{LaunchOverAdb,PackFlyThings}.java 等

# javap 交叉验证字符串常量（关键常量：ui/lib/bin/font/tr、EasyUI.cfg、/mnt/extsd、/tmp、ctl.restart）
& 'C:\Program Files\Java\jdk-21.0.11\bin\javap.exe' -p -c -constants `
  -classpath 'D:\zkswe\FlyThingsIDE\bin\plugins\com.zkswe.ide.editor_2.0.0.202608170922.jar' `
  com.zksw.editor.handler.LaunchOverAdb

# 在依赖库里搜 setting.ini / wpa_supplicant
cmd /c 'findstr /S /M /C:"setting.ini" "D:\zkswe\FlyThingsIDE\bin\.dep\*" "D:\zkswe\FlyThingsIDE\sdk\*"'

# 从 .a/.so 提取可打印字符串（本例用 pwsh 读字节流，≥5 字符）
```

### A.3 上一级 Agent 需要知道的两个"行动项差异"

1. **`wifiOnAndWait()` 语义被高估**：它不是"联网完成"，只是"射频已开"。§2.3 有硬证据。
2. **凭据首选源应改为 `/data/misc/wifi/wpa_supplicant.conf`**（官方 `base::parseWpaSsidAndPsk`），`setting.ini`（`[main] wifiSsid/wifiPwd`）作为第二来源——因为替换 zkgui 后 `setting.ini` 不再被官方应用维护，会变陈旧。

### A.4 未解问题（建议真机验证，按性价比排序）

| # | 待验证 | 验证方法（只读） |
|---|---|---|
| 1 | 真机 `setting.ini` 原文（段、键序、`wifiPwd` 是否明文/是否可能为空） | `adb shell cat /data/setting.ini`（注意：文件含云端 token，**不要外传/提交**） |
| 2 | 开机自动连的真实耗时（决定 `kAutoConnectWaitSec`） | 上电后 `adb shell logcat -v time` 看 wpa_supplicant 关联日志 |
| 3 | `getConnectionInfo()->getPasswd()` 在自动连场景是否非空 | 临时打印 `getPasswd().empty()`（仅诊断版，不留主线） |
| 4 | 我们 App 是否仍会让 `persist.wifi.on` 保持 1 / 由谁拉起 wpa_supplicant | `adb shell getprop persist.wifi.on`、`getprop init.svc.wpa_supplicant` |
