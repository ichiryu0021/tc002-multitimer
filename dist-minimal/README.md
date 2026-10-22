# Minimal /res image (no Ulanzi stock assets)

This directory contains a self-contained **minimal** `/res` image for TC002 / Z21.
It does **not** include the official Ulanzi stock `/res` fonts, app icons, audio,
BlueZ tools, stock web pages or test UI.

## Files in the image (6)

| Path | Origin |
|---|---|
| `lib/libzkgui.so` | This project, GPL-3.0-or-later (built from `native/MultiTimer`) |
| `lib/libssl.so.1.1` | Debian armhf OpenSSL 1.1.1w (OpenSSL License) |
| `lib/libcrypto.so.1.1` | Debian armhf OpenSSL 1.1.1w (OpenSSL License) |
| `ui/main.ftu` | Ulanzi official demo resource (GPL-3.0-or-later) |
| `ui/cacert.pem` | Public CA bundle from <https://curl.se/ca/cacert.pem> (MPL-2.0) |
| `etc/EasyUI.cfg` | This project (points at `/res/lib` and `/res/ui`) |

The device's system `/lib` must provide the official firmware runtime
(`libeasyui`, `libzkhardware`, `libzknet`, …); this image only replaces `/res`.

## Artifact

- `update.img` — 1,491,516 bytes
- SHA256 `B079F8DA0A3443D4B8E21D0B5D2BA51284F1537400630260ECF8CEA9E012F4F2`
- `update.img.sha256`, `build-info-minimal.txt`

## Build

```powershell
powershell -ExecutionPolicy Bypass -File tools\make-minimal-image.ps1 `
  -SoPath build\v3g-workspace\MultiTimer\Release\libzkgui.so `
  -OutDir build\minimal-image
```

## Flash

Use the official ADB flow from `tools/flash-update.ps1 -Image dist-minimal\update.img -Confirm`.
Make sure you can restore the official firmware with the USB-C reset button first.
