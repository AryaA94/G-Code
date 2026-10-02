# web/: the browser version

A single HTML page with the C++ library compiled to WebAssembly inside it,
and a hand-written UI on top. Published at <https://aryaa94.github.io/G-Code/>.

- `head.html`: page structure and CSS, ends with an open `<script>`.
- `wasm_bindings.cpp`: the three functions the page calls, `gs_analyze`,
  `gs_render_svg` and `gs_rules`. They take strings and return JSON.
- `ui_logic.js`: everything the page does: the editor, the 3D view
  (orbit/pan/zoom), playback, the stats, the diagnostics list, the speed chart.
- `build_web.sh`: compiles `../src` and `wasm_bindings.cpp` with Emscripten
  and joins `head.html` + the example programs + the engine + `ui_logic.js`
  into `dist/gcode-sim-web.html`.
- `tests/ui_test.mjs`: end-to-end test in headless Chromium.

Rebuild after changing the library or the UI (needs `em++` on PATH, see the
top of the script, and a normal CMake configure first for nlohmann/json):

```sh
./build_web.sh
```

Test (needs the native CLI in `../build/gcode-sim`):

```sh
cd tests && npm install && npx playwright install chromium && node ui_test.mjs
```

For every example it checks that the page's cycle time, lengths, move counts
and diagnostics equal what `gcode-sim` prints, then clicks through the editor,
the diagnostics, a broken machine config and playback.

**Publishing:** `../.github/workflows/pages.yml` runs that test and deploys
`dist/gcode-sim-web.html` on every push to `main`. It deploys the committed
file, so rebuild and commit `dist/` when the library or UI changes. One-time
setup: Settings -> Pages -> Source: "GitHub Actions".
