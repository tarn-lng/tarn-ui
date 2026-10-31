#!/usr/bin/env bash
# Test/build orchestration; the fixed recipe itself executes entirely in Tarn.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-$(command -v tarn)}"
evidence="$root/.build/build-tests"
mkdir -p "$evidence/source"
cp "$root/tests/launcher/project-build.tarn" "$evidence/source/main.tarn"
cp "$root/devtools/build.tarn" "$root/devtools/process_run.tarn" "$root/devtools/wire.tarn" "$evidence/source/"
"$compiler" build "$evidence/source/main.tarn" -o "$evidence/probe" --json > "$evidence/compiler.jsonl"
if rg -q '"severity":"warning"' "$evidence/compiler.jsonl"; then
    cat "$evidence/compiler.jsonl" >&2
    exit 1
fi
"$evidence/probe" "$root" "$compiler" > "$evidence/success.json" 2> "$evidence/success.stderr"
fixture=$(mktemp -d "$evidence/invalid.XXXXXXXX")
printf '%s\n' "$fixture" > "$evidence/invalid-root.txt"
mkdir -p "$fixture/core" "$fixture/examples/counter" "$fixture/backends/linux/gtk"
cp "$root/core/session.tarn" "$root/core/reactive.tarn" "$fixture/core/"
cp "$root/examples/counter/counter.tarn" "$fixture/examples/counter/"
cp "$root"/backends/linux/gtk/{gtk,native,protocol,devtools}.tarn "$fixture/backends/linux/gtk/"
printf 'fn main() { missing_function() }\n' > "$fixture/examples/counter/main.tarn"
set +e
"$evidence/probe" "$fixture" "$compiler" > "$evidence/failure.stdout" 2> "$evidence/failure.json"
status=$?
set -e
test "$status" -eq 1
test ! -s "$evidence/failure.stdout"
test ! -s "$evidence/success.stderr"
rg -q '"code":"BUILD_FAILED"' "$evidence/failure.json"
rg -q 'missing_function' "$evidence/failure.json"
rg -q '"diagnostics":\[' "$evidence/failure.json"
printf '2 project build checks passed (real success and compiler rejection)\n'
