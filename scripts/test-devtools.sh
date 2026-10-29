#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# == 0 || ( $# == 1 && $1 == --build-only ) ]] || { echo 'Usage: test-devtools.sh [--build-only]' >&2; exit 2; }
build="$root/.build/devtools-tests"
compiler="${TARN_BIN:-$root/.build/verified-da3173c/debug/tarn}"
pkg-config --atleast-version=1.10 json-glib-1.0
mkdir -p "$build/source/core" "$root/.build/evidence/phase3"
cp "$root/core/session.tarn" "$root/core/reactive.tarn" "$build/source/core/"
cp "$root/examples/counter/counter.tarn" "$build/source/counter.tarn"
cp "$root/tests/devtools/app.tarn" "$build/source/main.tarn"
source "$root/scripts/backend.sh"
ui_backend "$build" -DTARN_UI_DEVTOOLS -DTARN_UI_TESTING
LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" \
    "${UI_LINKS[@]}" -o "$build/counter" --json > "$build/compiler.jsonl"
[[ ${1:-} != --build-only ]] || exit 0
exec python3 "$root/tests/devtools/test_protocol.py"
