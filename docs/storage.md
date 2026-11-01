# Shared filesystem and TPMS settings

MAIN owns the filesystem mount and application policy. On the shared 16 MiB S3, `project` occupies 0xC00000..0xFFFFFF (4 MiB), mounted once as SPIFFS at `/project`. The independent 8 MiB headless profile reserves its final 4 MiB at 0x400000. A profile is not permission to repartition another board.

Never format on mount failure, erase NVS, upload a complete filesystem image or wildcard-clean the shared volume. A failed mount leaves the application running with explicit storage failure and private compile-time defaults; it does not authorize destructive recovery.

Wi-Fi profiles are separate from this filesystem contract. NaWFstore uses the existing `nvs` partition and only its `nawfstore` namespace, keys `cfg0` and `cfg1`. Each bounded binary blob starts with `NWF1`, followed by a little-endian generation, startup policy, length-prefixed profiles and CRC32. Save writes the inactive key, commits and verifies readback before publishing; the previous generation remains. Invalid existing configurations are reported without replacing them with compiled defaults. Credentials are not encrypted by this serialization format; NVS encryption is a separate device configuration. NaWFstore never formats SPIFFS or erases NVS.

| Namespace | Owner | Use |
|---|---|---|
| `tpms.cfg.0`, `tpms.cfg.1` | MAIN TPMS | Complete known-instrument registry, garage and wheel assignments |
| `tpms.diag.0`, `tpms.diag.1` | MAIN TPMS | Temporary bounded trip diagnostics |
| `bt.*` | BT application adapter | Reserved for future BT-specific persistence |
| `ui.*` or explicitly agreed `ui/` | UI application adapter | Future UI persistence; no automatic legacy migration |
| `web.*` | Web application adapter | Future web-specific persistence |
| `catalog.0`, `catalog.1`, `[01][st]NNN.cbor` | Existing UI demo | Legacy catalog/documents; preserve every file |

These are ownership rules for applications, not a requirement that the public libraries mount or know this partition. All writers must use the MAIN coordinator: one lock for free-space admission and the complete write, including transient generation size. Independent free-space checks do not enforce a shared reserve. Keep at least 1 MiB free; each subsystem also needs its own bounded quota. MAIN currently admits settings and journal writes under the same FS mutex. The UI project remains read-only in ROM, so CatalogProjectStore is not silently claimed to implement MAIN's budget/readback contract.

## Commit and recovery

Settings are one coherent JSON document, bounded to 32 KiB. It includes `version`, optimistic `revision`, `active`, `known` and `garage`. Cross-file transactions are avoided: registry changes and references live in the same generation.

Optional version-2 fields are `wifiEnabled`, `wifiWindowSeconds`, `bleIntervalMs`, `bleWindowMs` and `nativeTheme`. Older documents default to Wi-Fi enabled without a deadline, BLE 80/80 ms and Cream. `radio.put` validates the Wi-Fi boolean, duration 0..86400 seconds, BLE timings 5..10000 ms in steps of 5 and window <= interval. BLE always runs; there is no saved enable flag or session timer for it. Changing scan timings preserves engine-owned devices and received measurements. `appearance.put` validates one of five scheme IDs (light, cream, emerald, dark, night) and commits through the same two-slot transaction. Runtime Wi-Fi overrides do not replace startup policy; LCD Settings can open Wi-Fi for five minutes even if its saved startup policy is disabled.

Each slot starts with `TPMSCFG1`, then little-endian uint64 generation, payload length and CRC32, followed by UTF-8 JSON. Generation must equal document revision + 1. Load validates header, exact size, checksum, schema and references, selecting the greatest valid generation. One damaged slot can fall back to the other. If both existing slots are invalid, writes are disabled; the files are preserved.

Commit truncates only the inactive owned slot, writes a complete generation, flushes, fsyncs, closes, reads it back and checks the decoded document before publishing the new in-memory settings. The previous active slot remains intact. An error returns failure without applying the proposal. SPIFFS itself is not a power-loss-proof journaled filesystem; physical brownout/corruption recovery is not certified by this implementation. No mount-time autoformat or automatic repair is used.

A worker serializes settings and log I/O. The main BLE/UI loop copies a committed settings snapshot when its revision changes. HTTP submits at most four commands, each at most 4096 bytes, and polls bounded operation results (latest sixteen). Version conflicts and invalid assignments do not change revision or the active state.

## Domain

A known instrument has immutable system `id`, `driver`, `transport`, `protocol`, protocol-internal `protocolId`/`identity`, optional numeric `code`, and observed `transportIdentity`. It has no user name or wheel position. BLE/TomTom's protocol ID is the normalized six-digit sticker code; numeric `code` is a parser helper. New discoveries receive a stable app ID derived from the BLE address and address type, independent of advertising SID. Imported IDs remain unchanged so garage references survive. Existing records for unavailable drivers remain readable; that does not advertise support for those drivers.

Schema version 2 removes old sensor names and adds the driver/transport identity fields. Loading version 1 preserves all registry IDs, vehicle names, axle/wheel geometry and assignments, then commits a verified version 2 generation into the inactive slot. It never guesses a wheel position from a sensor's former name.

The compiled `TpmsTransport`/`TpmsProtocol` enums and `tpms_drivers` descriptors live in `tpms_types.hpp`. `APP_TPMS_BLE_TOMTOM` (default 1, overridable with `-DAPP_TPMS_BLE_TOMTOM=0`) gates both the configured driver and its advertised capability. The current build offers BLE/TomTom only. Runtime driver discovery is not implemented. `/api/tpms` reports this catalog, retained observations and search state.

There is no manual sensor creation or editing command: `known.put` is rejected. `known.bind` accepts only a `discoveryId` present in the device's parsed observations. The HTTP adapter copies transport, protocol and identity from its owned snapshot into a trusted storage job; client-supplied identity fields are ignored. The storage worker revalidates the compiled driver and the coherent document before committing. Observations are capped at 96; eviction prefers the oldest unregistered observation and never removes registered observations to make room for an unknown one. Overflow is reported by `dropped`.

Garage contains up to sixteen named vehicles. Each has 1..16 axles, each axle 1..8 wheels; total stored wheels are bounded to 128 and the encoded document to 32 KiB. A wheel has a name and optional known-instrument ID. Unknown references and using the same instrument twice within one vehicle are rejected. An instrument may be reused across different vehicles. Removing an assigned instrument is rejected until it is unassigned. Changing the selected vehicle does not clear the registry's received values. Persisted `active` is also the vehicle shown at boot on LCD and web. A star in the garage selects this default; for the currently edited vehicle, unsaved geometry/assignments and `active` are committed together by `vehicle.put` with `makeDefault:true`.

Instruments additionally carry optional free-form `tags`, independent of immutable identity and vehicle assignment. These are user text (Unicode supported), not a season enum: for example a storage location, set, vehicle or any personal label. `known.tags` edits only this metadata, using the same revision/transaction checks. Up to eight distinct nonempty tags, each up to 64 UTF-8 bytes, keep storage and telemetry bounded. Older version 2 records without this optional field load with an empty list. UI uses a comma/semicolon-separated editor opened explicitly through the tag icon; no predefined values are imposed. Compact tag text is shown on the tire and in the registry table, with full text in details/tooltip.

The initial private four-wheel set is imported only when neither settings slot exists. Firmware updates do not overwrite saved settings with compile-time defaults. Credentials are never part of this document.

## Finding and assigning a sensor

Starting or resetting search records a per-observation pressure baseline: its retained latest measurement, or the first packet if none has arrived. Newer packets are compared with that baseline in integer Pa. An absolute cumulative change of at least 10000 Pa (0.10 bar), positive or negative, latches a color marker until search is reset or stopped. Returning to baseline does not erase the marker. The API exposes baseline, current delta, peak delta and actual last-packet time; out-of-order/duplicate timestamps do not advance the pressure probe. This is evidence of a pressure change, not automatic identification of a wheel.

The reusable web `tpms-sensor` component displays protocol, transport, protocol ID, last pressure, tags and age as a narrow rounded tire. It is shared by discovery and the garage. The garage has the instrument pool on the left and vehicle on the right, with independent scrolling. Desktop drag/drop, touch hold/drag, or explicit sensor-then-wheel selection edit the vehicle draft. Moving between occupied positions in the same vehicle swaps their sensors, including across axles. Moving to an empty wheel clears the source; dropping onto the same position does nothing. Assigning an instrument that was not on this vehicle replaces the target relation while keeping the displaced instrument in the registry. One shared assignment function applies these rules to drag, touch and conventional selectors. Changes become durable only when the vehicle is saved. Geometry and conventional selectors remain under Axles and wheels. Live telemetry does not replace the draft or expire the last readings.

The vehicle is a top view with an upward front marker and stacked axle modules. A single place is centered; two places sit at opposite edges; four or more form left/right clusters connected by a thin axle. Arbitrary stored counts remain supported. Position names are not printed on the normal diagram and do not require hover instructions; accessibility labels and the explicit geometry editor preserve their meaning. Technical instrument details are opened separately. The diagram uses geometry and symbols rather than locale-specific placement labels.

Each vehicle occupies a bounded outlined block. Its preferred width depends on the widest axle, not the browser width: 112 px for a single place, otherwise 84 px per place plus 120 px for the axle gap and block padding. Narrow viewports can reduce the center gap; dense axles retain readable tire sizes and scroll horizontally. All axles share the same block width.

## Diagnostics and browser

The read-only browser lists and downloads all shared files in chunks, including binary catalogs. It has no generic upload, delete or format endpoint. `/api/file` rejects traversal. Concurrent downloads of a growing diagnostic file are best-effort; download after the trip with the board powered and radio quiet for a consistent final segment.

Trip logging runs for up to six hours after boot, with two files of at most 128 KiB each and a bounded queue of 32 records. Each record carries a boot identifier. Logs distinguish raw TomTom candidates, parser result, printed/raw code, admission, delivered readings and ten-second radio/parser/drop counters. Repeated raw/reading records are rate-limited per instrument. Logs are appended and flushed by the storage worker, never from a GAP callback or per HTTP request. Old log segments may be replaced; configuration and other namespaces are never rotated. No claim of receiving a missing RF packet is inferred from a successful web request.

Diagnostics version 2 logs requested and SDK-confirmed BLE interval/window changes, Wi-Fi start/connect/disconnect/stop events and reason codes, and ten-second Wi-Fi/scan/queue/storage counters. TomTom candidate and delivered-reading records are limited to one per second; exact per-instrument advertisement/reading totals in counters preserve evidence of faster bursts. Up to eight known sensors are included per counter record, cycling through larger registries to keep records bounded. Candidate address hints aid rejection diagnostics only and never admit a device. Wi-Fi credentials and SSIDs are not added to the filesystem journal. Higher detail reduces retained trip duration within the same fixed quota; copy logs after the test.
