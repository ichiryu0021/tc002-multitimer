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