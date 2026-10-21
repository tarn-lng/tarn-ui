#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-tarn}"
evidence="${EVIDENCE_DIR:-$root/evidence}"
mkdir -p "$evidence" "$root/.build"
failed=0
for name in callback-escape direct-store-escape local-store-control; do
    log="$evidence/borrow-effects-$name.log"
    if "$compiler" build "$root/tests/compiler/borrow-effects/$name.tarn" -o "$root/.build/$name" > "$log" 2>&1; then
        printf 'FAIL: %s accepted; not executed\n' "$name"
        failed=1
    elif ! grep -Eq 'error\[E(410|42)' "$log"; then
        printf 'FAIL: %s rejected without a lifetime/borrow diagnostic\n' "$name"
        failed=1
    else
        printf 'PASS: %s rejected for a borrow/lifetime violation\n' "$name"
    fi
done
"$compiler" build "$root/tests/compiler/borrow-effects/callback-valid.tarn" -o "$root/.build/callback-valid"
[[ $("$root/.build/callback-valid") == 42 ]]
printf 'PASS: callback copying an owned scalar compiles and prints 42\n'
exit "$failed"
