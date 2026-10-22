# 第三方组件声明 (Third-Party Notices)

本仓库主体代码采用 [GPL-3.0-or-later](LICENSE) 发布。

## OpenSSL 1.1.1w（armhf 预编译库）

- 位置：`native/MultiTimer/res-extra/lib/libssl.so.1.1`、`libcrypto.so.1.1`
- 许可：OpenSSL License / Original SSLeay License
- 来源：Debian 11 (bullseye) `libssl1.1_1.1.1w-0+deb11u1_armhf.deb`
- 完整许可：`native/MultiTimer/third_party/openssl-armhf/LICENSE`
- 用途：设备端 B 站 HTTPS 直连，随 `/res` 镜像安装到 `/res/lib`。

## TomThumb 5px 字体

- 位置：`native/MultiTimer/src/Fonts/TomThumb.h`
- 许可：BSD-style（保留 Adafruit / Brian J. Swetland / Robey Pointer 版权声明）
- 来源：<https://github.com/adafruit/Adafruit-GFX-Library>

## FlyThings SDK

本仓库不直接分发 FlyThings SDK 及其依赖包；`native/MultiTimer/Manifest.xml` 只通过包名和版本号引用，编译时由 FlyThings IDE 从官方依赖仓库拉取。各依赖包许可以 FlyThings 官方说明为准。

## Ulanzi 官方 stock `/res`

本仓库不包含官方固件镜像 `update.img` 或 stock `/res` 基线（含厂商 UI、字体、库等）。构建镜像前请自备官方固件，并从自己的设备/固件包抽取 stock `/res`。

## 说明

如发现遗漏或版权问题，请提交 Issue 联系维护者。
## Ulanzi 官方开源仓库（上游）

- 仓库：<https://github.com/UlanziTechnology/Ulanzi-U-Clock-TC002>
- 许可：GPL-3.0-or-later
- 本项目的 `PageBase`、`KeyManager`、`McuManager`、`TomThumb` 等部分文件复制/改写自该官方仓库；保留官方版权声明，本项目整体继续以 GPL-3.0-or-later 发布。
- 官方仓库只包含源码/示例/协议文档，不包含官方 stock `/res` 固件资源；本公开仓库因此不包含 `update.img`。
## Public CA bundle（最小镜像 `/res/ui/cacert.pem`）

- 来源：<https://curl.se/ca/cacert.pem>
- 许可：Mozilla Public License 2.0（完整文本见 `native/MultiTimer/third_party/ca-bundle/LICENSE-MPL-2.0.txt`）
- 用途：Bilibili HTTPS 证书链/主机名校验；非 Ulanzi 专有内容。

## FlyThings SDK / 运行库

本项目源码通过 FlyThings IDE 依赖包与官方 TC002 Demo 构建；完整镜像不包含 SDK 安装包。设备系统 `/lib` 中的 `libeasyui`、`libzkhardware`、`libzknet` 等运行库由用户设备的官方固件提供。FlyThings 依赖包许可以官方说明为准；官方 TC002 仓库的 `THIRD_PARTY_NOTICES.md` 列出了 BlueZ / Adafruit GFX / FlyThings 组件的许可。