#!/usr/bin/env bash
# Builds examples/cairn_b1_rects against backends/cairn (pure-Tarn renderer).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="${TARN_UI_BUILD_DIR:-$root/.build/cairn_b1_rects}"
[[ $build == /* ]] || { echo 'TARN_UI_BUILD_DIR must be absolute' >&2; exit 2; }
compiler="${TARN_BIN:-$(command -v tarn)}"
cairn="$root/backends/cairn/backend"
mkdir -p "$build/source/backend" "$build/source/core"
cp "$cairn/damage.tarn" "$cairn/render.tarn" "$cairn/png.tarn" \
   "$cairn/text.tarn" "$cairn/icons.tarn" "$cairn/anim.tarn" \
   "$cairn/objc.tarn" "$cairn/surface_macos.tarn" "$cairn/surface_linux.tarn" \
   "$cairn/scene_b1.tarn" "$cairn/scene_b2.tarn" "$cairn/scene_b3.tarn" "$cairn/native.tarn" "$build/source/backend/"
cp "$cairn/devtools_off.tarn" "$build/source/backend/devtools.tarn"
cp "$root/core/session.tarn" "$build/source/core/session.tarn"
cp "$root/examples/cairn_b1_rects/main.tarn" "$build/source/main.tarn"

if [[ $(uname -s) == Darwin ]]; then
    sdk=$(xcrun --show-sdk-path)
    ln -sf "$sdk/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation.tbd" \
        "$build/libCoreFoundation.tbd"
    ln -sf "$sdk/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics.tbd" \
        "$build/libCoreGraphics.tbd"
    UI_LINKS=(--link objc --link CoreFoundation --link CoreGraphics)
else
    UI_LINKS=()
fi

if ! LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" \
    "${UI_LINKS[@]}" -o "$build/cairn_b1_rects" --json > "$build/compiler.jsonl"; then
    cat "$build/compiler.jsonl" >&2
    exit 1
fi
if grep -q '"severity":"warning"' "$build/compiler.jsonl"; then
    cat "$build/compiler.jsonl" >&2
    exit 1
fi
printf 'Built %s/cairn_b1_rects\n' "$build"
printf 'Run: nice -n 10 bash scripts/run-cairn-b1.sh\n'
