#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# == 0 || ( $# == 1 && $1 == --dev ) ]] || { echo 'Usage: build-navigation.sh [--dev]' >&2; exit 2; }
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
build="$root/.build/navigation"
packages=(gtk4)
flags=()
if [[ ${1:-} == --dev ]]; then
    pkg-config --atleast-version=1.10 json-glib-1.0
    build="$root/.build/navigation-dev"
    packages+=(json-glib-1.0)
    flags+=(-DTARN_UI_DEVTOOLS)
fi
mkdir -p "$build/source/core"
cp "$root/core/session.tarn" "$root/core/components.tarn" "$root/core/navigation.tarn" "$root/core/forms.tarn" "$build/source/core/"
cp "$root/examples/navigation/tasks.tarn" "$build/source/tasks.tarn"
cp "$root/examples/navigation/main.tarn" "$build/source/main.tarn"
cc -std=c11 -Wall -Wextra -Werror -fPIC -shared "${flags[@]}" \
    "$root/backends/linux/gtk/bridge.c" -o "$build/libtarn_ui_gtk.so" \
    $(pkg-config --cflags --libs "${packages[@]}")
if ! LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" --link tarn_ui_gtk --link glib-2.0 \
    -o "$build/navigation" --json > "$build/compiler.jsonl"; then cat "$build/compiler.jsonl" >&2; exit 1; fi
if rg -q '"severity":"warning"' "$build/compiler.jsonl"; then cat "$build/compiler.jsonl" >&2; exit 1; fi
{
    "$compiler" version
    pkg-config --modversion "${packages[@]}"
    sha256sum "$compiler" "$root/core/session.tarn" "$root/core/components.tarn" "$root/core/navigation.tarn" "$root/core/forms.tarn" \
        "$root/examples/navigation/main.tarn" "$root/examples/navigation/tasks.tarn" "$root/backends/linux/gtk/bridge.c" "$root/backends/linux/gtk/bridge.h" \
        "$build/libtarn_ui_gtk.so"
    if [[ ${1:-} == --dev ]]; then sha256sum "$root/backends/linux/gtk/devtools.inc"; fi
    printf 'ABI=14\nLD_LIBRARY_PATH=%s\n' "$build"
} > "$build/provenance.txt"
printf 'Built %s/navigation\n' "$build"
