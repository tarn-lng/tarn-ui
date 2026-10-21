#!/usr/bin/env bash
# Compilation orchestration; all launcher/client fixtures execute in Tarn.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
build="$root/.build/launcher-tests"
mkdir -p "$build"
TARN_BIN="$compiler" bash "$root/scripts/build-launcher.sh"
for source in process-contract fixture acceptance mcp-acceptance recursive-error scalar-error-control; do
    if ! "$compiler" build "$root/tests/launcher/$source.tarn" -o "$build/$source" --json > "$build/$source-compiler.jsonl"; then
        cat "$build/$source-compiler.jsonl" >&2
        exit 1
    fi
    if rg -q '"severity":"warning"' "$build/$source-compiler.jsonl"; then
        cat "$build/$source-compiler.jsonl" >&2
        exit 1
    fi
done
for source in recursive-error scalar-error-control; do
    "$build/$source" > "$build/$source.stdout" 2> "$build/$source.stderr"
    [[ $(cat "$build/$source.stdout") == false ]]
    [[ ! -s "$build/$source.stderr" ]]
    printf 'PASS: %s compiles and runs\n' "$source"
done
run() {
    local name=$1
    shift
    local status=0
    "$@" > "$build/$name.jsonl" 2> "$build/$name.stderr" || status=$?
    printf '%s\n' "$status" > "$build/$name.exit"
    cat "$build/$name.jsonl"
    if (( status != 0 )) || [[ -s "$build/$name.stderr" ]]; then
        cat "$build/$name.stderr" >&2
        exit 1
    fi
}
run contract "$build/process-contract" "$root/.build/counter-dev/counter" "$root/.build/counter-dev"
run acceptance "$build/acceptance" "$root/.build/launcher/tarn-ui" "$root/.build/counter-dev/counter" "$root/.build/counter-dev" "$build/fixture"
run mcp "$build/mcp-acceptance" "$root/.build/launcher/tarn-ui" "$root/.build/counter-dev/counter" "$root/.build/counter-dev" "$build/fixture"
