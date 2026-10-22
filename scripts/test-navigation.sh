#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# == 0 || ( $# == 1 && $1 == --build-only ) ]] || { echo 'Usage: test-navigation.sh [--build-only]' >&2; exit 2; }
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
build="$root/.build/navigation-tests"
bash "$root/scripts/build-navigation.sh" --dev
mkdir -p "$build/source/core"
cp "$root/core/navigation.tarn" "$root/core/session.tarn" "$root/core/components.tarn" "$build/source/core/"
cp "$root/tests/navigation/state.tarn" "$build/source/main.tarn"
"$compiler" build "$build/source/main.tarn" -o "$build/state" --json > "$build/state.compiler.jsonl"
if rg -q '"severity":"warning"' "$build/state.compiler.jsonl"; then cat "$build/state.compiler.jsonl" >&2; exit 1; fi
"$build/state" > "$build/state.log" 2>&1
cc -std=c11 -Wall -Wextra -Werror -g -fPIC -shared -DTARN_UI_TESTING -DTARN_UI_DEVTOOLS \
    "$root/backends/linux/gtk/bridge.c" -o "$build/libtarn_ui_gtk.so" \
    $(pkg-config --cflags --libs gtk4 json-glib-1.0)
cp "$root/tests/navigation/views.tarn" "$build/source/views.tarn"
if ! LIBRARY_PATH="$build" "$compiler" build "$build/source/views.tarn" --link tarn_ui_gtk \
    -o "$build/views" --json > "$build/views.compiler.jsonl"; then cat "$build/views.compiler.jsonl" >&2; exit 1; fi
if rg -q '"severity":"warning"' "$build/views.compiler.jsonl"; then cat "$build/views.compiler.jsonl" >&2; exit 1; fi
cc -std=c11 -Wall -Wextra -Werror -g -DTARN_UI_TESTING \
    "$root/backends/linux/gtk/bridge.c" "$root/tests/navigation/lifecycle.c" \
    -o "$build/lifecycle" $(pkg-config --cflags --libs gtk4)
[[ ${1:-} != --build-only ]] || exit 0
bash "$root/scripts/build-launcher.sh"
timeout 30s "$build/lifecycle" > "$build/lifecycle.log" 2>&1
status=0
python3 "$root/tests/navigation/test_navigation.py" > "$build/native.log" 2>&1 || status=$?
cat "$build/native.log"
exit "$status"
