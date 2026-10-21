#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/devtools-memory"
compiler="${TARN_BIN:-$root/.build/verified-da3173c/debug/tarn}"
mkdir -p "$build/source/core" "$root/.build/evidence/phase3"
cp "$root/core/session.tarn" "$build/source/core/session.tarn"
cp "$root/examples/counter/counter.tarn" "$build/source/counter.tarn"
cp "$root/tests/devtools/app.tarn" "$build/source/main.tarn"
cc -std=c11 -Wall -Wextra -Werror -g -fPIC -shared \
    -DTARN_UI_DEVTOOLS -DTARN_UI_TESTING -fsanitize=address,undefined \
    "$root/backends/linux/gtk/bridge.c" -o "$build/libtarn_ui_gtk.so" \
    $(pkg-config --cflags --libs gtk4 json-glib-1.0)
LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" \
    --link tarn_ui_gtk -o "$build/counter" --json > "$build/compiler.jsonl"
exec python3 "$root/tests/devtools/memory_probe.py"
