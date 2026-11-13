# Build dependencies

ESP-IDF Component Manager fetches these Git revisions from the root of each
repository. The application manifest is `src/idf_component.yml`.
`dependencies.lock` records the resolved revisions and hashes.

| Component key | Git repository | Commit |
| --- | --- | --- |
| `nabtools` | https://github.com/NaBrIks/NaBTools.git | `a9a821b6408309fa260b78524ffd79db9456aa35` |
| `nacboruiforlvgl` | https://github.com/NaBrIks/NaCBORUIforLVGL.git | `533c2df4f6caf4b7ee0165cf55901c9b6770afd3` |
| `nawserver` | https://github.com/NaBrIks/NaWServer.git | `318732c3aca99d8af36f0b9373d126a9e339ef8e` |

All three Git revisions are publicly accessible. The application selects
`CONFIG_NABTOOLS_SIG_OFF=y`, so the build uses NaBTools without the optional
Bluetooth SIG name tables. If a generated sdkconfig retains a previous SIG
choice, select `OFF` in `pio run -e <environment> -t menuconfig`.
Registry pins remain LVGL 9.6.0, TinyCBOR 0.6.1~4, esp_lcd_touch 1.2.1 and
esp_lcd_touch_gt911 1.1.3.

## Firmware build

The checked-in `platformio.ini` pins Espressif32 8.0.0 and the toolchains. Its
`symlink://` entry requires an existing ESP-IDF 5.5.6 PlatformIO package under
`${platformio.packages_dir}/framework-espidf-v5.5.6`. Provision that SDK before
building on another machine. Component Manager 2.5.2 was used for verification.

```powershell
pio run -e s3-lcd7
```

The first build fetches the components into `managed_components`. It uses the
tracked CBOR files in `src/ui` and web assets in `src/www`. NaWServer supplies
the HTTP, WebSocket and SSE APIs. The application embeds its own assets.

## Regenerate the assets

After the first build, install the downloaded UI workspace's locked Node
dependencies and run the generators:

```powershell
npm ci --prefix managed_components/nacboruiforlvgl --ignore-scripts
node tools/encode-screen.mjs
node tools/build-web.mjs
pio run -e s3-lcd7
```

Both the firmware appearance sources and the Node generator read the shared
appearance module from that same revision. The downloaded component, its
`node_modules`, and build logs stay outside version control.

All four repositories share the owner-approved Uncrustify 0.83.0_f profile.
Its SHA-256 as stored in Git (UTF-8 with LF line endings) is
`3be39942f18335d9a89956cc2c710de872f3372db5b3c8e172f4c66c8cc2a457`.
Third-party material and generated files are excluded from formatting; see
[the code style guide](../agents/code_style.md) and
[the license scope](../LICENSES.md).

## Offline rebuild

Once the locked components are provisioned in `managed_components`, a rebuild
can use them with the manager disabled:

```powershell
$env:IDF_COMPONENT_MANAGER = '0'
pio run -e s3-lcd7
```

Remove the process environment setting to resume dependency resolution.

## Validation on 2026-11-13

An isolated checkout of `4e97f7e` with the current application changes built
`s3-lcd7` using the locked Git and registry dependencies. The build reported
2,656,201 bytes of flash usage and 63,588 bytes of static RAM. The generators
reproduced the tracked project CBOR, appearance CBOR and embedded HTML byte for
byte. Verification covered compilation, linking and asset generation.
