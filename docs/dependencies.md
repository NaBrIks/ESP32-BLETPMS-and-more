# Repository build baseline

The application uses independent sibling repositories. Library implementations
are not copied into this application. These published revisions form the
2026-10-30 compatibility baseline:

| Checkout directory | Canonical repository | Default branch | Revision |
| --- | --- | --- | --- |
| `../NaBTools` | [NaBrIks/NaBTools](https://github.com/NaBrIks/NaBTools) | `main` | `e3e7b2c6f80b7086d45b7ed67b8122f2ff312354` |
| `../NaCBORUIforLVGL` | [NaBrIks/NaCBORUIforLVGL](https://github.com/NaBrIks/NaCBORUIforLVGL) | `master` | `7d5963b8c2b2170edacea6b8317e1d364de27cfc` |
| `../NaWServer` | [NaBrIks/NaWServer](https://github.com/NaBrIks/NaWServer) | `main` | `117fadffc62a6c8b0b2a170d0a34125883707583` |

The repositories currently retain their private visibility. GitHub access to
them is required. NaWServer starts with one root commit; the other two libraries
retain their histories. A later default-branch HEAD is not automatically the
same tested baseline.

From a fresh application checkout, create the sibling directories:

```powershell
git clone https://github.com/NaBrIks/NaBTools.git ../NaBTools
git clone https://github.com/NaBrIks/NaCBORUIforLVGL.git ../NaCBORUIforLVGL
git clone https://github.com/NaBrIks/NaWServer.git ../NaWServer
git -C ../NaBTools checkout e3e7b2c6f80b7086d45b7ed67b8122f2ff312354
git -C ../NaCBORUIforLVGL checkout 7d5963b8c2b2170edacea6b8317e1d364de27cfc
git -C ../NaWServer checkout 117fadffc62a6c8b0b2a170d0a34125883707583
npm --prefix ../NaCBORUIforLVGL ci
node tools/encode-screen.mjs
node tools/build-web.mjs
pio run -e s3-lcd7
```

Do not run clone commands over existing working directories. Keep unrelated
work intact and use an isolated parent directory when reproducing this baseline.

The toolchain is PlatformIO Espressif32 8.0.0 with the ESP-IDF 5.5.6 package at
the location configured in `platformio.ini`. Managed component versions are
recorded in `dependencies.lock`: LVGL 9.6.0, espressif/cbor 0.6.1~4,
esp_lcd_touch 1.2.1 and esp_lcd_touch_gt911 1.1.3. cJSON is supplied by ESP-IDF.
No private sensor or Wi-Fi header is required for compilation.

All four repositories share the owner-approved Uncrustify 0.83.0_f profile.
Its SHA-256 as stored in Git (UTF-8 with LF line endings) is
`3be39942f18335d9a89956cc2c710de872f3372db5b3c8e172f4c66c8cc2a457`.
Third-party material and generated files are excluded from formatting; see
[the code style guide](../agents/code_style.md) and
[the license scope](../LICENSES.md).

## Validation

On 2026-10-30 the application code at `420f606` was built from an isolated
GitHub clone with the library revisions above. No source files or private
Wi-Fi/sensor headers were copied from the working checkouts. The installed
ESP-IDF/toolchain packages were reused as build prerequisites.

- Both generators reproduced the committed CBOR and web assets without changes.
- `pio run -e s3-lcd7`: PASS; RAM 61,084 bytes, flash 2,563,477 bytes.
- Application native TPMS probe/discovery tests, with TomTom enabled and
  disabled, and wheel-assignment checks: PASS.
- The application's 29 maintained C/C++ files pass Uncrustify Check; their
  lexical tokens and string contents match the pre-format version after
  excluding comments and whitespace.

Local build logs and artifacts remain outside version control. Hardware
acceptance of a different board profile is separate from this compilation check.
