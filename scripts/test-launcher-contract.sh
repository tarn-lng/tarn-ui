#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
build="$root/.build/launcher-tests"
mkdir -p "$build"
"$compiler" build "$root/tests/launcher/process-contract.tarn" -o "$build/process-contract" --json > "$build/compiler.jsonl"
if rg -q '"severity":"warning"' "$build/compiler.jsonl"; then
    cat "$build/compiler.jsonl" >&2
    exit 1
fi
exec "$build/process-contract" "$root/.build/counter-dev/counter" "$root/.build/counter-dev"
