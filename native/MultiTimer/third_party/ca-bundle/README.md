# Public CA bundle

- File: `native/MultiTimer/res-minimal/ui/cacert.pem`（200,116 B）
- Source: <https://curl.se/ca/cacert.pem>（curl CA extract, generated from Mozilla CA list）
- License: Mozilla Public License 2.0（见 `LICENSE-MPL-2.0.txt`）
- Purpose: `/res/ui/cacert.pem` used by `TlsClient` for Bilibili HTTPS hostname/chain verification.
- Regenerate: `curl -fsSL https://curl.se/ca/cacert.pem -o ui/cacert.pem`

This bundle is public CA data and is not Ulanzi proprietary content.
