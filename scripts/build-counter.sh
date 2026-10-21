#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/counter"
compiler="${TARN_BIN:-$root/.build/verified-da3173c/debug/tarn}"
bash "$root/scripts/checkout-tarn.sh"
mkdir -p "$build/source/core"
cp "$root/core/session.tarn" "$build/source/core/session.tarn"
cp "$root/examples/counter/main.tarn" "$build/source/main.tarn"
cp "$root/examples/counter/counter.tarn" "$build/source/counter.tarn"
# pkg-config output is split into cc arguments, never eval'd or passed to Tarn.
cc -std=c11 -Wall -Wextra -Werror -fPIC -shared \
    "$root/backends/linux/gtk/bridge.c" -o "$build/libtarn_ui_gtk.so" \
    $(pkg-config --cflags --libs gtk4)
LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" \
    --link tarn_ui_gtk -o "$build/counter" --json > "$build/compiler.jsonl"
{
    cc --version | head -n 1
    "$compiler" --version
    pkg-config --modversion gtk4
    sha256sum "$root/backends/linux/gtk/bridge.c" \
        "$root/backends/linux/gtk/bridge.h" "$root/core/session.tarn" \
        "$compiler" "$build/libtarn_ui_gtk.so"
    sha256sum "$root/examples/counter/counter.tarn" "$root/examples/counter/main.tarn"
    printf 'ABI=2\nLIBRARY_PATH=%s\nLD_LIBRARY_PATH=%s\n' "$build" "$build"
} > "$build/provenance.txt"
printf 'Built %s\nRun: bash scripts/run-counter.sh\n' "$build/counter"
