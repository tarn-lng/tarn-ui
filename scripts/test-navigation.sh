#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# == 0 || ( $# == 1 && $1 == --build-only ) ]] || { echo 'Usage: test-navigation.sh [--build-only]' >&2; exit 2; }
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
build="$root/.build/navigation-tests"
bash "$root/scripts/build-navigation.sh" --dev
mkdir -p "$build/source/core"
cp "$root/core/forms.tarn" "$root/core/navigation.tarn" "$root/core/session.tarn" "$root/core/components.tarn" "$build/source/core/"
cp "$root/examples/navigation/tasks.tarn" "$build/source/tasks.tarn"
source "$root/scripts/backend.sh"
ui_backend "$build" -DTARN_UI_TESTING -DTARN_UI_DEVTOOLS
cp "$root/tests/navigation/state.tarn" "$build/source/main.tarn"
LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" "${UI_PLAIN_LINKS[@]}" -o "$build/state" --json > "$build/state.compiler.jsonl"
if rg -q '"severity":"warning"' "$build/state.compiler.jsonl"; then cat "$build/state.compiler.jsonl" >&2; exit 1; fi
"$build/state" > "$build/state.log" 2>&1
cp "$root/tests/navigation/forms.tarn" "$build/source/forms-test.tarn"
LIBRARY_PATH="$build" "$compiler" build "$build/source/forms-test.tarn" "${UI_PLAIN_LINKS[@]}" -o "$build/forms-test" --json > "$build/forms.compiler.jsonl"
if rg -q '"severity":"warning"' "$build/forms.compiler.jsonl"; then cat "$build/forms.compiler.jsonl" >&2; exit 1; fi
"$build/forms-test" > "$build/forms.log" 2>&1
cp "$root/tests/navigation/tasks.tarn" "$build/source/tasks-test.tarn"
LIBRARY_PATH="$build" "$compiler" build "$build/source/tasks-test.tarn" "${UI_PLAIN_LINKS[@]}" -o "$build/tasks-test" --json > "$build/tasks.compiler.jsonl"
if rg -q '"severity":"warning"' "$build/tasks.compiler.jsonl"; then cat "$build/tasks.compiler.jsonl" >&2; exit 1; fi
"$build/tasks-test" > "$build/tasks.log" 2>&1
cp "$root/tests/navigation/views.tarn" "$build/source/views.tarn"
if ! LIBRARY_PATH="$build" "$compiler" build "$build/source/views.tarn" "${UI_LINKS[@]}" \
    -o "$build/views" --json > "$build/views.compiler.jsonl"; then cat "$build/views.compiler.jsonl" >&2; exit 1; fi
if rg -q '"severity":"warning"' "$build/views.compiler.jsonl"; then cat "$build/views.compiler.jsonl" >&2; exit 1; fi
if ui_c_backend; then
    cc -std=c11 -Wall -Wextra -Werror -g -DTARN_UI_TESTING \
        "$root/backends/linux/gtk/bridge.c" "$root/tests/navigation/lifecycle.c" \
        -o "$build/lifecycle" $(pkg-config --cflags --libs gtk4)
fi
[[ ${1:-} != --build-only ]] || exit 0
bash "$root/scripts/build-launcher.sh"
if ui_c_backend; then timeout 30s "$build/lifecycle" > "$build/lifecycle.log" 2>&1; fi
status=0
python3 "$root/tests/navigation/test_navigation.py" > "$build/native.log" 2>&1 || status=$?
cat "$build/native.log"
exit "$status"
