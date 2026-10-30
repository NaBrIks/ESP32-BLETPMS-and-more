# License scope and third-party notices

The root [MIT License](LICENSE) applies to independently authored application code,
tools, tests and documentation, by the owner's decision of 2026-10-30. Existing
third-party copyrights, licenses and notices remain in force.

| Material | Applicable notice |
| --- | --- |
| Independently authored application code, tools and tests | MIT; short SPDX headers identify source files |
| `src/boards/lcd7/board.cpp` | Our additions are MIT; the referenced Waveshare/Espressif reset/init adaptation retains [CC0-1.0](LICENSES/CC0-1.0.txt), copyright 2022 Espressif Systems (Shanghai) CO LTD. SPDX expression: `MIT AND CC0-1.0`. See the pinned upstream source in PROVENANCE.md |
| `tools/generate-ui.mjs`, `src/ui/project.json` and generated `project.cbor` | Project-owned UI adaptation, MIT by the owner's explicit choice; its source NaCBORUIforLVGL example now records the same license |
| Generated `src/ui/fonts/app_text14.c` and `app_text20.c` | [Montserrat OFL](docs/font-montserrat-license.txt) and [DejaVu notices](docs/font-dejavu-license.txt); generated data is not relabelled as MIT |
| Tabler icon font and car SVG | [Upstream MIT notice](docs/tabler-icons-license.txt) |
| Vendored LVGL in the archived `Example` tree | Its adjacent `LICENCE.txt` and per-file notices |
| Vendored Espressif LED strip component in `Example` | Its adjacent `LICENSE` and per-file notices |
| ESP-IDF, LVGL, TinyCBOR and touch components | Their upstream licenses, installed with the selected SDK/component versions |
| Sibling NaBTools, NaCBORUIforLVGL and NaWServer | Each repository's license scope and exceptions; this application does not relicense them |

[PROVENANCE.md](PROVENANCE.md) records the adaptation chain. An MIT label on our
independent additions does not replace a notice belonging to an upstream file or
asset. A fresh Git history is not a change of source provenance. Generated CBOR
and strict JSON have no inserted comment headers; their scope is recorded here.
