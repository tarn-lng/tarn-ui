#!/usr/bin/env bash
# Test harness only; runtime permission handling remains entirely in Tarn.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
evidence="$root/.build/evidence/phase4-completion"
mkdir -p "$evidence"
"$compiler" build "$root/tests/launcher/private-creation.tarn" -o "$evidence/private-creation" --json > "$evidence/private-creation.compiler.jsonl"
if rg -q '"severity":"warning"' "$evidence/private-creation.compiler.jsonl"; then
    cat "$evidence/private-creation.compiler.jsonl" >&2
    exit 1
fi
probe=$(mktemp -d "$evidence/modes.XXXXXXXX")
# Exercise the exact original failing environment in the test subprocess only.
(umask 022; "$evidence/private-creation" "$probe/private") > "$evidence/private-creation.stdout" 2> "$evidence/private-creation.stderr"
test "$(stat -c '%a' "$probe/private")" = 700
test "$(stat -c '%a' "$probe/private/capture.bin")" = 600
stat -c '%a %n' "$probe/private" "$probe/private/capture.bin" > "$evidence/private-creation.modes"
test ! -s "$evidence/private-creation.stderr"
cat "$evidence/private-creation.stdout" "$evidence/private-creation.modes"
