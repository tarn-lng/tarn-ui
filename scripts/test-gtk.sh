#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/gtk-tests"
evidence="${EVIDENCE_DIR:-$root/.build/evidence/phase2}"
compiler="${TARN_BIN:-$root/.build/verified-da3173c/debug/tarn}"
[[ $# == 0 || ( $# == 1 && $1 == --build-only ) ]] || {
    echo 'Usage: test-gtk.sh [--build-only]' >&2; exit 2;
}
mkdir -p "$build/source/core" "$evidence"
cp "$root/core/session.tarn" "$root/core/reactive.tarn" "$build/source/core/"
cp "$root/tests/gtk/counter-smoke.tarn" "$build/source/main.tarn"
cp "$root/examples/counter/counter.tarn" "$build/source/counter.tarn"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -DTARN_UI_TESTING "$root/backends/linux/gtk/bridge.c" \
    "$root/tests/gtk/lifecycle.c" -o "$build/lifecycle" \
    $(pkg-config --cflags --libs gtk4)
cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -DTARN_UI_TESTING \
    "$root/backends/linux/gtk/bridge.c" -o "$build/libtarn_ui_gtk.so" \
    $(pkg-config --cflags --libs gtk4)
LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" \
    --link tarn_ui_gtk --link glib-2.0 -o "$build/smoke" --json > "$build/compiler.jsonl"
cp "$root/tests/gtk/session-smoke.tarn" "$build/source/session-smoke.tarn"
LIBRARY_PATH="$build" "$compiler" build "$build/source/session-smoke.tarn" \
    --link tarn_ui_gtk --link glib-2.0 -o "$build/session-smoke" --json > "$build/session-compiler.jsonl"
for entry in private-construction:E3014 task-transfer:E3047 borrow-escape:E4210 overlapping-borrows:E4101; do
    name=${entry%%:*}
    code=${entry#*:}
    cp "$root/tests/gtk/$name.tarn" "$build/source/$name.tarn"
    log="$evidence/$name.jsonl"
    if LIBRARY_PATH="$build" "$compiler" build "$build/source/$name.tarn" \
        --link tarn_ui_gtk --link glib-2.0 -o "$build/invalid-$name" --json > "$log" 2>&1; then
        echo "FAIL: $name accepted; binary NOT executed" >&2
        exit 1
    fi
    rg -q "\"code\":\"$code" "$log"
    printf 'PASS: %s rejected (%s)\n' "$name" "$code"
done
if [[ ${1:-} == --build-only ]]; then exit 0; fi
# Preserve every result and still test Tarn if the native leak check fails.
# LeakSanitizer stays enabled. No blanket GTK leak suppression.
result=0
for mode in bridge control; do
    args=("$evidence/window.png")
    if [[ $mode == control ]]; then
        args=(--gtk-control "$evidence/control.png")
    fi
    status=0
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
        timeout 30s "$build/lifecycle" "${args[@]}" \
        > "$evidence/$mode.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$evidence/$mode.exit"
    printf '%s: exit %s\n' "$mode" "$status"
    (( status == 0 )) || result=1
done
for test in smoke session-smoke; do
    status=0
    LD_LIBRARY_PATH="$build" timeout 20s "$build/$test" \
        > "$evidence/tarn-$test.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$evidence/tarn-$test.exit"
    printf 'Tarn %s: exit %s\n' "$test" "$status"
    (( status == 0 )) || result=1
done
exit "$result"
