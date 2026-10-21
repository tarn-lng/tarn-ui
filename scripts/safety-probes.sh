#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-tarn}"
evidence="${EVIDENCE_DIR:-$root/evidence}"
mkdir -p "$evidence" "$root/.build"
failed=0
for entry in private-construction:E3014 task-transfer:E3047 borrow-escape:; do
    name=${entry%%:*}
    code=${entry#*:}
    log="$evidence/safety-$name.log"
    if "$compiler" build "$root/tests/compiler/scoped-session/$name.tarn" -o "$root/.build/safety-$name" > "$log" 2>&1; then
        printf 'FAIL: %s was accepted; binary was NOT executed\n' "$name"
        failed=1
    elif [[ -n "$code" ]] && ! grep -q "error\[$code\]" "$log"; then
        printf 'FAIL: %s did not produce %s\n' "$name" "$code"
        failed=1
    elif [[ -z "$code" ]] && ! grep -q 'error\[E42' "$log"; then
        printf 'FAIL: %s did not produce a borrow diagnostic\n' "$name"
        failed=1
    else
        printf 'PASS: %s rejected\n' "$name"
    fi
done
exit "$failed"
