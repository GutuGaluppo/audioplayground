#!/usr/bin/env bash
# Builds the browser host (ADR-016) with Emscripten and copies the module where the UI serves it.
#
#   tools/web/build.sh            Release build into build/web
#   tools/web/build.sh Debug      Debug build (assertions, no optimisation)
#
# Needs emcc on the PATH (brew install emscripten, or an emsdk shell). Offline-friendly: if a
# nlohmann_json source tree from a desktop build exists it is reused instead of downloaded.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
type="${1:-Release}"
build="$root/build/web"

command -v emcmake >/dev/null || { echo "emcmake not found: install Emscripten (brew install emscripten)" >&2; exit 1; }

extra=()
for candidate in "$root"/build/*/_deps/nlohmann_json-src; do
    if [ -d "$candidate" ]; then
        extra+=("-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$candidate")
        break
    fi
done

# Native WebAssembly exceptions: the core reports bad project data by throwing.
emcmake cmake -S "$root" -B "$build" \
    -DCMAKE_BUILD_TYPE="$type" \
    -DAP_BUILD_DESKTOP=OFF -DAP_BUILD_TESTS=OFF -DAP_BUILD_WEB=ON \
    -DCMAKE_CXX_FLAGS="-fwasm-exceptions" \
    -DCMAKE_EXE_LINKER_FLAGS="-fwasm-exceptions" \
    ${extra[@]+"${extra[@]}"}
cmake --build "$build" --target ap_web --parallel

mkdir -p "$root/ui/public/engine"
cp "$build/apps/web/ap_web.wasm" "$root/ui/public/engine/ap_web.wasm"
ls -l "$root/ui/public/engine/ap_web.wasm"
