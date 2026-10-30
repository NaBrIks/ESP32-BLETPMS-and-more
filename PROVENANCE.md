# Source provenance

Independently authored application code is MIT by the owner's decision of
2026-10-30. Third-party and adapted materials retain their applicable licenses;
see [LICENSES.md](LICENSES.md) for the exact scope and retained notices.

- `src/boards/lcd7/board.cpp` and `board.hpp` are adapted from the NaBTools research LCD7 example, which adapts NaCBORUIforLVGL's S3 style-lab board integration and the original NaWServer touch_lcd7 example. Those project-owned adaptations are now MIT by the owner's explicit choice. Referenced reset/init portions of the original [Waveshare/Espressif C source](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7/blob/b30c6be50d3d60b9426f84439cd368fe0499dbeb/examples/ESP-IDF/08_lvgl_v8_demo/components/waveshare_rgb_lcd_port.c) retain CC0-1.0 and the 2022 Espressif notice; the manufacturer's repository-level Apache license is not substituted for that per-file notice. These are application board adapters, not copies of library runtimes. Frame ownership remains unchanged.
- `src/ui/project.json` adapts the four-sensor TPMS screen and styles from `NaCBORUIforLVGL/examples/s3_style_lab/project/demo.mjs`, whose project-owned code is MIT under the same owner decision. Demonstration values and unrelated GPS components are removed; application-owned data sources drive the existing widgets through generic bindings. A compact variant uses the same document model.
- The LilyGO board adapter preserves the original project's GPIO map, panel timing and ST7789 command sequence. DMA completion now controls LVGL buffer reuse.
- Bluetooth SIG data, protocol parsing and device drivers belong to NaBTools. The old application submodule and local implementation are retired.
- HTTP/WebSocket/SSE mechanisms belong to NaWServer. No local server implementation or compatibility container is maintained here.
- Web layout follows the owner's TechnicalShell density, table alignment and color rules; detailed rules are in `docs/ui-principles.md`.

- The shared shell CSS/themes, local Tabler font and tooltip module adapt UI.TechnicalShell. Tabler license is retained in docs/tabler-icons-license.txt. The local font subset is supplemented by the official Tabler outline car SVG in src/www/icons/car.svg (https://github.com/tabler/tabler-icons/blob/main/icons/outline/car.svg), inlined into the embedded page.
- app_text14.c and app_text20.c are subsets generated with lv_font_conv 1.5.3 from LVGL's bundled Montserrat-Medium.ttf (Latin) and DejaVuSans.ttf (Cyrillic). The respective OFL and DejaVu licenses are retained in docs/font-*-license.txt. Generated C headers contain the reproducible converter arguments; source TTFs remain managed SDK dependencies.
