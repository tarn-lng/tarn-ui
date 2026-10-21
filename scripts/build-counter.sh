#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# == 0 || ( $# == 1 && $1 == --dev ) ]] || { echo 'Usage: build-counter.sh [--dev]' >&2; exit 2; }
build="$root/.build/counter"
flags=()
packages=(gtk4)
if [[ ${1:-} == --dev ]]; then
    pkg-config --atleast-version=1.10 json-glib-1.0
    build="$root/.build/counter-dev"
    flags=(-DTARN_UI_DEVTOOLS)
    packages+=(json-glib-1.0)
fi
compiler="${TARN_BIN:-$root/.build/verified-da3173c/debug/tarn}"
bash "$root/scripts/checkout-tarn.sh"
mkdir -p "$build/source/core"
cp "$root/core/session.tarn" "$build/source/core/session.tarn"
cp "$root/examples/counter/main.tarn" "$build/source/main.tarn"
cp "$root/examples/counter/counter.tarn" "$build/source/counter.tarn"
# pkg-config output is split into cc arguments, never eval'd or passed to Tarn.
cc -std=c11 -Wall -Wextra -Werror -fPIC -shared "${flags[@]}" \
    "$root/backends/linux/gtk/bridge.c" -o "$build/libtarn_ui_gtk.so" \
    $(pkg-config --cflags --libs "${packages[@]}")
LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" \
    --link tarn_ui_gtk -o "$build/counter" --json > "$build/compiler.jsonl"
{
    cc --version | head -n 1
    "$compiler" --version
    pkg-config --modversion "${packages[@]}"
    sha256sum "$root/backends/linux/gtk/bridge.c" \
        "$root/backends/linux/gtk/bridge.h" "$root/core/session.tarn" \
        "$compiler" "$build/libtarn_ui_gtk.so"
    sha256sum "$root/examples/counter/counter.tarn" "$root/examples/counter/main.tarn"
    if [[ ${1:-} == --dev ]]; then sha256sum "$root/backends/linux/gtk/devtools.inc"; fi
    printf 'ABI=3\nLIBRARY_PATH=%s\nLD_LIBRARY_PATH=%s\n' "$build" "$build"
} > "$build/provenance.txt"
printf 'Built %s\n' "$build/counter"
if [[ ${1:-} == --dev ]]; then
    printf 'Launch explicitly: python3 scripts/run-dev-counter.py --allow inspect click close --allow-text\n'
else
    printf 'Run: bash scripts/run-counter.sh\n'
fi
