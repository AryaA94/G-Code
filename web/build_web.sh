#!/usr/bin/env bash
# web/build_web.sh
#
# Compiles the C++ library to WebAssembly and glues it together with the UI
# into one self-contained file: dist/gcode-sim-web.html
#
#   head.html          page structure and CSS (ends with an open <script>)
#   (generated)        the example programs and machine.json from ../examples
#   (generated)        the compiled engine, WASM embedded as base64
#   ui_logic.js        everything the page does
#
# Needs Emscripten's em++ on PATH:
#   git clone https://github.com/emscripten-core/emsdk.git
#   cd emsdk && ./emsdk install latest && ./emsdk activate latest
#   source ./emsdk_env.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB="$ROOT/web"
BUILD="$WEB/_wasm_build"
DIST="$WEB/dist"
JSON_INCLUDE="${JSON_INCLUDE:-}"

if ! command -v em++ &> /dev/null; then
    echo "error: em++ not found on PATH. See the comment at the top of this script." >&2
    exit 1
fi

# nlohmann/json is header-only; reuse the copy CMake already downloaded.
if [[ -z "$JSON_INCLUDE" ]]; then
    for d in "$ROOT"/build*/_deps/nlohmann_json-src/include; do
        [[ -f "$d/nlohmann/json.hpp" ]] && JSON_INCLUDE="$d" && break
    done
fi
if [[ -z "$JSON_INCLUDE" ]]; then
    echo "error: nlohmann/json not found. Run a normal CMake configure first, or set JSON_INCLUDE." >&2
    exit 1
fi

mkdir -p "$BUILD" "$DIST"

echo "== Compiling to WASM =="
# SINGLE_FILE_BINARY_ENCODE=0 keeps the base64 encoding: raw bytes inside a
# <script> tag can be mangled by the browser.
em++ -std=c++20 -O2 -fexceptions \
  -I "$ROOT/include" -I "$JSON_INCLUDE" \
  "$ROOT"/src/*.cpp "$WEB/wasm_bindings.cpp" \
  -o "$BUILD/gcodesim_wasm.js" \
  -s MODULARIZE=1 -s EXPORT_NAME=GcodeSimModule \
  -s EXPORTED_FUNCTIONS="['_gs_analyze','_gs_render_svg','_gs_rules']" \
  -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap']" \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s SINGLE_FILE=1 \
  -s SINGLE_FILE_BINARY_ENCODE=0 \
  -s ENVIRONMENT=web \
  -s DISABLE_EXCEPTION_CATCHING=0 \
  --no-entry

echo "== Embedding examples =="
python3 - "$ROOT" > "$BUILD/examples.js" << 'PYEOF'
import json, sys
from pathlib import Path
ex = Path(sys.argv[1]) / "examples"
programs = {p.stem: p.read_text() for p in sorted(ex.glob("*.nc"))}
print("const GS_EXAMPLES = " + json.dumps(programs) + ";")
print("const GS_MACHINE = " + json.dumps((ex / "machine.json").read_text()) + ";")
PYEOF

echo "== Assembling the page =="
{
    cat "$WEB/head.html"
    cat "$BUILD/examples.js"
    cat "$BUILD/gcodesim_wasm.js"
    cat "$WEB/ui_logic.js"
    echo "</script></body></html>"
} > "$DIST/gcode-sim-web.html"

echo "done: $DIST/gcode-sim-web.html ($(wc -c < "$DIST/gcode-sim-web.html") bytes)"
