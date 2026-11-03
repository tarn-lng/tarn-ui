#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/devtools-memory"
compiler="${TARN_BIN:-tarn}"
mkdir -p "$build/source/core" "$root/.build/evidence/phase3"
cp "$root/core/session.tarn" "$root/core/reactive.tarn" "$build/source/core/"
cp "$root/examples/counter/counter.tarn" "$build/source/counter.tarn"
cp "$root/tests/devtools/app.tarn" "$build/source/main.tarn"
# The probe preloads ASan into the app; the Tarn backend is plain code in it.
source "$root/scripts/backend.sh"
ui_backend "$build" -DTARN_UI_DEVTOOLS -DTARN_UI_TESTING
LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" \
    "${UI_LINKS[@]}" -o "$build/counter" --json > "$build/compiler.jsonl"
exec python3 "$root/tests/devtools/memory_probe.py"
