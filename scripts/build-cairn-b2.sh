#!/usr/bin/env bash
# Builds examples/cairn_b2_welcome against backends/cairn (pure-Tarn renderer + text/icon engine).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="${TARN_UI_BUILD_DIR:-$root/.build/cairn_b2_welcome}"
[[ $build == /* ]] || { echo 'TARN_UI_BUILD_DIR must be absolute' >&2; exit 2; }
compiler="${TARN_BIN:-$(command -v tarn)}"
cairn="$root/backends/cairn/backend"
mkdir -p "$build/source/backend" "$build/source/core"
cp "$cairn/damage.tarn" "$cairn/render.tarn" "$cairn/png.tarn" \
   "$cairn/text.tarn" "$cairn/icons.tarn" "$cairn/anim.tarn" \
   "$cairn/theme.tarn" "$cairn/layout.tarn" \
   "$cairn/objc.tarn" "$cairn/surface_macos.tarn" "$cairn/surface_linux.tarn" \
   "$cairn/scene_b1.tarn" "$cairn/scene_b2.tarn" "$cairn/scene_b3.tarn" \
   "$cairn/scene_app.tarn" "$cairn/native.tarn" "$build/source/backend/"
cp "$cairn/devtools_off.tarn" "$build/source/backend/devtools.tarn"
cp "$root/core/session.tarn" "$build/source/core/session.tarn"
cp "$root/examples/cairn_b2_welcome/main.tarn" "$build/source/main.tarn"
ln -sfn "$root/backends/cairn/fonts" "$build/fonts"

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
    "${UI_LINKS[@]}" -o "$build/cairn_b2_welcome" --json > "$build/compiler.jsonl"; then
    cat "$build/compiler.jsonl" >&2
    exit 1
fi
if grep -q '"severity":"warning"' "$build/compiler.jsonl"; then
    cat "$build/compiler.jsonl" >&2
    exit 1
fi
printf 'Built %s/cairn_b2_welcome\n' "$build"
printf 'Run: nice -n 10 bash scripts/run-cairn-b2.sh\n'
