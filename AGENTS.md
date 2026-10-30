# Application integration

- This is the main application. NaBTools, NaCBORUIforLVGL and NaWServer are independent sibling libraries intended for public reuse.
- Read Readme.md (Russian: Readme.ru.md) and docs/architecture.md before changes. Keep application policy, credentials, board profiles and product settings here; reusable library mechanisms belong to their owning repositories.
- Before creating, changing or assessing UI, read and apply docs/ui-principles.md in full. It is the mandatory owner standard.
- Use NaBTools::runtime through the ESP-IDF component. Do not restore a local Bluetooth implementation or a legacy API layer.
- UI screens, styles and bindings are NaCBORUIforLVGL project documents in src/ui/project.json. Generate CBOR with node tools/encode-screen.mjs; do not hand-code product widgets or per-label event handlers.
- HTTP, WebSocket and SSE use NaWServer as a sibling component. Never restore local lib implementations. Keep transport callbacks separate from the application/UI state owner.
- Common runtime code must not depend on a particular GPIO map or physical display. Board sources are selected in CMake; headless operation does not create LVGL objects.
- Hardware is shared with other chats. Coordinate ownership before opening serial or flashing. Identify the chip/MAC and check the installed partition table. Never flash another connected board implicitly.
- Preserve NVS and the existing final project-storage region. Do not add automatic NVS erase or filesystem formatting. MAIN owns the shared 4 MiB FS contract in docs/storage.md. Use only your namespace, serialize admission and writes, preserve the common 1 MiB reserve and previous valid generation.
- Keep credentials, private addresses, serial logs, builds and device evidence in ignored local/ or other ignored storage. Public documentation uses neutral examples.
- Do not occupy port 443. Keep full project URLs with an explicit port. Register confirmed useful URLs in the owner's local portal through its normal registration API.
- Do not commit or push unless requested.
- Owner decision 2026-10-30: independently authored code is MIT; preserve third-party licenses and provenance. Use short SPDX headers for authored source files; never relabel vendored or adapted material without checking its origin.
- Before writing or formatting C/C++, read agents/code_style.md (Russian: agents/code_style.ru.md). Use the shared Uncrustify 0.83.0_f profile in uncrustify.cfg, not a competing formatter. The owner authorized applying this profile across the main application and the three sibling libraries on 2026-10-30. Exclude vendored sources, archived Example components, and generated fonts/assets.
