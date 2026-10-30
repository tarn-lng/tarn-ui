#!/usr/bin/env bash
# Runs the Tarn GTK backend's tests with `tarn test` (docs/bridge-migration.md):
# the devtools protocol unit tests, then the native smokes, one process each.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/native"
compiler="${TARN_BIN:-tarn}"
gtk="$root/backends/linux/gtk"
rm -rf "$build"
mkdir -p "$build/source/gtk"
cp "$gtk"/gtk.tarn "$gtk"/native.tarn "$gtk"/protocol.tarn "$gtk"/protocol_test.tarn "$build/source/gtk/"
result=0
"$compiler" test "$build/source/gtk/protocol.tarn" || result=1
# The runner discovers *_test.tarn siblings of the entry module.
for test in "$root"/tests/native/*.tarn; do
    cp "$test" "$build/source/$(basename "$test" .tarn)_test.tarn"
done
printf '// Entry for the native smokes; tests live in the *_test.tarn siblings.\n' > "$build/source/smokes.tarn"
G_DEBUG=fatal-criticals "$compiler" test "$build/source/smokes.tarn" --timeout 20000 \
    --link gtk-4 --link gobject-2.0 --link gio-2.0 --link glib-2.0 || result=1
exit "$result"
