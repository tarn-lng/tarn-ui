#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
[[ $# == 0 || ( $# == 1 && $1 == --build-only ) ]] || { echo 'Usage: test-reactive.sh [--build-only]' >&2; exit 2; }
build="$root/.build/reactive-tests"
mkdir -p "$build/source/core"
cp "$root/core/session.tarn" "$root/core/reactive.tarn" "$build/source/core/"
cp "$root/examples/counter/counter.tarn" "$build/source/"
cc -std=c11 -Wall -Wextra -Werror -g -fPIC -shared \
    -DTARN_UI_DEVTOOLS -DTARN_UI_TESTING "$root/backends/linux/gtk/bridge.c" \
    -o "$build/libtarn_ui_gtk.so" $(pkg-config --cflags --libs gtk4 json-glib-1.0)
for name in state app example; do
    if [[ $name == example ]]; then
        cp "$root/examples/counter/main.tarn" "$build/source/example.tarn"
    else
        cp "$root/tests/reactive/$name.tarn" "$build/source/$name.tarn"
    fi
    LIBRARY_PATH="$build" "$compiler" build "$build/source/$name.tarn" \
        --link tarn_ui_gtk --link glib-2.0 -o "$build/$name" --json > "$build/$name.compiler.jsonl"
    if rg -q '"severity":"warning"' "$build/$name.compiler.jsonl"; then
        cat "$build/$name.compiler.jsonl" >&2; exit 1
    fi
done
cc -std=c11 -Wall -Wextra -Werror -g -DTARN_UI_TESTING \
    "$root/backends/linux/gtk/bridge.c" "$root/tests/gtk/lifecycle.c" \
    -o "$build/native-lifecycle" $(pkg-config --cflags --libs gtk4)
LD_LIBRARY_PATH="$build" "$build/state" > "$build/state.log" 2>&1
[[ ${1:-} != --build-only ]] || exit 0
# Functional lifecycle only; the deferred sanitizer investigation has its own script.
timeout 30s "$build/native-lifecycle" "$build/lifecycle.png" > "$build/lifecycle.log" 2>&1
status=0
python3 "$root/tests/reactive/test_reactive.py" > "$build/native.log" 2>&1 || status=$?
cat "$build/native.log"
exit "$status"
