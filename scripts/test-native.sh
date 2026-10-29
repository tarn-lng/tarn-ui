#!/usr/bin/env bash
# Builds and runs the smokes of the Tarn GTK backend (docs/bridge-migration.md).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/native"
compiler="${TARN_BIN:-tarn}"
rm -rf "$build"
mkdir -p "$build/source/gtk"
cp "$root"/backends/linux/gtk/*.tarn "$build/source/gtk/"
result=0
for test in "$root"/tests/native/*.tarn; do
    name=$(basename "$test" .tarn)
    cp "$test" "$build/source/$name.tarn"
    "$compiler" build "$build/source/$name.tarn" -o "$build/$name" \
        --link gtk-4 --link gobject-2.0 --link gio-2.0 --link glib-2.0
    status=0
    G_DEBUG=fatal-criticals timeout 20s "$build/$name" || status=$?
    printf '%s: exit %s\n' "$name" "$status"
    (( status == 0 )) || result=1
done
exit "$result"
