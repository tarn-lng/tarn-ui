#!/usr/bin/env bash
# Builds existing unaltered tarn_ui examples (counter, navigation, widgets)
# against backends/cairn (Milestone B4: ABI 14 parity + Cairn theme & layout).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-$(command -v tarn)}"
cairn="$root/backends/cairn/backend"

build_example() {
    local name=$1
    local build="${TARN_UI_BUILD_DIR:-$root/.build/cairn_b4_$name}"
    [[ $build == /* ]] || { echo 'TARN_UI_BUILD_DIR must be absolute' >&2; exit 2; }
    mkdir -p "$build/source/backend" "$build/source/core"
    cp "$cairn/damage.tarn" "$cairn/render.tarn" "$cairn/png.tarn" \
       "$cairn/text.tarn" "$cairn/icons.tarn" "$cairn/anim.tarn" \
       "$cairn/theme.tarn" "$cairn/layout.tarn" \
       "$cairn/objc.tarn" "$cairn/surface_macos.tarn" "$cairn/surface_linux.tarn" \
       "$cairn/scene_b1.tarn" "$cairn/scene_b2.tarn" "$cairn/scene_b3.tarn" \
       "$cairn/scene_app.tarn" "$cairn/b5_model.tarn" "$cairn/scene_b5.tarn" "$cairn/b6_vfs.tarn" "$cairn/scene_b6.tarn" "$cairn/compositor.tarn" "$cairn/native.tarn" "$build/source/backend/"
    cp "$cairn/devtools_off.tarn" "$build/source/backend/devtools.tarn"
    cp "$root/core/session.tarn" "$root/core/reactive.tarn" "$root/core/components.tarn" \
       "$root/core/navigation.tarn" "$root/core/forms.tarn" "$build/source/core/"
    cp "$root/examples/$name/"*.tarn "$build/source/"
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
        "${UI_LINKS[@]}" -o "$build/$name" --json > "$build/compiler.jsonl"; then
        cat "$build/compiler.jsonl" >&2
        exit 1
    fi
    if grep -q '"severity":"warning"' "$build/compiler.jsonl"; then
        cat "$build/compiler.jsonl" >&2
        exit 1
    fi
    printf 'Built %s/%s (against backends/cairn)\n' "$build" "$name"
}

if [[ $# -eq 0 ]]; then
    build_example counter
    build_example navigation
    build_example widgets
else
    for ex in "$@"; do
        build_example "$ex"
    done
fi
printf 'Run: nice -n 10 bash scripts/run-cairn-b4.sh [counter|navigation|widgets]\n'
