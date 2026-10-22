# OpenSSL 1.1.1w（armhf，仅用于 TC002 / Z21 的 B 站直连）

设备系统自带的 `/lib/libssl.so.1.1` 是 OpenSSL 1.1.0i 的定制裁剪版：所有 TLS client
method 都被编译掉（`SSL_connect` 对任何站点返回 `141640BF no protocols available`，
raw server 收到 0 字节 ClientHello）。它不能用于 B 站/抖音的 HTTPS。

本目录与 `native/MultiTimer/res-extra/lib/` 携带 Debian 11 (bullseye) 的 armhf
OpenSSL 1.1.1w 动态库，随 `/res` 镜像安装到 `/res/lib/`；`TlsClient` 会优先
`dlopen` 这两个文件，再回退到系统库。

- 来源：<https://deb.debian.org/debian/pool/main/o/openssl/libssl1.1_1.1.1w-0+deb11u1_armhf.deb>
- 版本：OpenSSL 1.1.1w（2023-09-11），armhf，ELF32 ARM EABI
- 文件：`libssl.so.1.1`（352,540 B）、`libcrypto.so.1.1`（1,737,128 B）
- glibc 需求：最高 `GLIBC_2.17`；设备 glibc 2.30，兼容
- 许可：OpenSSL License / Original SSLeay License，见 `LICENSE`（Debian copyright）
- 用途：仅设备端 B 站直连 HTTPS；抖音/快手仍走局域网 bridge
- 不使用系统 CA 之外的证书；CA 仍为设备 `/res/ui/cacert.pem`

升级/替换时保持文件名与 SONAME 不变，并同步更新本 README、镜像 hash 与文档。
