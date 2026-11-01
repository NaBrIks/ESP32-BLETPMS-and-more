# ESP32-BLETPMS-and-more

English | [Русский](Readme.ru.md)

An ESP32 TPMS application built on three independent libraries: NaBTools for
Bluetooth, NaCBORUIforLVGL for CBOR-defined screens and bindings, and NaWServer
for HTTP, WebSocket and SSE. Application settings, vehicle layouts, sensor
assignments and board integration live here. There are no local library copies.

## Build

Place the three library checkouts next to this repository as `../NaBTools`,
`../NaCBORUIforLVGL` and `../NaWServer`. The current application requires
NaBTools advertisement classification and the `EspIdfScanTiming` /
`set_scan_timing` API; an older library HEAD is insufficient. Published repository
URLs, pinned revisions and fresh-checkout commands are in
[the repository build baseline](docs/dependencies.md).

The build uses PlatformIO Espressif32 8.0.0, ESP-IDF 5.5.6 and pinned toolchains.
`platformio.ini` expects the SDK in the PlatformIO package directory as
`framework-espidf-v5.5.6`. Install the UI codec dependencies before generating
the embedded project:

```powershell
npm --prefix ../NaCBORUIforLVGL ci
node tools/encode-screen.mjs
node tools/build-web.mjs
pio run -e s3-lcd7
```

Profiles: `s3-lcd7` (800×480), `lilygo-t-display-s3` (320×170) and
`s3-headless`. Verify the actual board, chip and installed partition table before
flashing. Preserve NVS and the existing final 4 MiB project filesystem;
do not erase flash or upload a replacement filesystem image.

Wi-Fi profiles are saved by [NaWFstore](lib/NaWFstore/README.md) in NVS and can
be changed without reflashing: use `wifi help` on the 115200-baud console or
the authenticated `/setup` page. The native Settings page has an `AP 5 min`
button and shows the temporary access point's connection details. Private
first-use defaults may be supplied in ignored `src/modules/wifi/wifi_secret.hpp`
using `WIFI_SSID` / `WIFI_PASS` and optional `WIFI_ALT_SSID` / `WIFI_ALT_PASS`;
they never overwrite an existing NVS configuration. A first-boot sensor set may be supplied in
ignored `src/modules/static_sensors_secret.hpp`; see the adjacent example header.
Firmware updates do not overwrite an existing persisted garage with that set.

## Operation

The default web page shows the active vehicle from above, with pressure,
temperature and seconds since each sensor's last reading. Last readings remain
visible after radio contact is lost. The garage supports configurable axles and
wheel positions, free-text sensor tags, drag-and-drop assignment and a default
vehicle. Moving a sensor between occupied wheel positions swaps them.

Separate pages provide TomTom observations, general Bluetooth research, files,
known sensors, the garage and radio settings. BLE reception remains enabled;
the scan interval and window are configurable. Wi-Fi can be enabled or disabled
and given a session duration. Display appearance uses shared UI-library styles,
including a black/red night palette. Product screens and bindings are stored in
`src/ui/project.json` and compiled to CBOR.

Bounded diagnostic files `tpms.diag.0` and `tpms.diag.1` record reception, parsing
and sensor admission for car tests. Shared storage is application-owned and
transactional; library components do not mount or overwrite it independently.

## Development and licenses

Use Uncrustify 0.83.0_f with the checked-in `uncrustify.cfg`:

```powershell
.\uncrustify-check.cmd
.\uncrustify-apply.cmd
```

The first command checks; the second formats tracked C/C++. Both exclude
vendored components, the archived `Example` tree and generated fonts/assets.
Read the [owner's C/C++ rules](agents/code_style.md) before editing.

Independently authored code is [MIT](LICENSE). Adapted board/UI materials,
fonts, icons and dependencies retain their licenses and notices; see
[LICENSES.md](LICENSES.md) and [PROVENANCE.md](PROVENANCE.md).

Further details: [architecture](docs/architecture.md),
[shared storage](docs/storage.md), [validation](docs/verification.md).
