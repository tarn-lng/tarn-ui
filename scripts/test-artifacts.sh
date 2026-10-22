#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
evidence="$root/.build/artifact-tests"
mkdir -p "$evidence/source"
cp "$root/tests/launcher/artifact-contract.tarn" "$evidence/source/main.tarn"
cp "$root/devtools/artifact.tarn" "$root/devtools/wire.tarn" "$evidence/source/"
"$compiler" build "$evidence/source/main.tarn" -o "$evidence/artifact-contract" --json > "$evidence/compiler.jsonl"
if rg -q '"severity":"warning"' "$evidence/compiler.jsonl"; then
    cat "$evidence/compiler.jsonl" >&2
    exit 1
fi
read -r png < "$root/.build/launcher-tests/saved-capture.path"
parent=$(mktemp -d "$evidence/store.XXXXXXXX")
(umask 022; "$evidence/artifact-contract" "$png" "$parent/private") > "$evidence/results.jsonl" 2> "$evidence/runtime.stderr"
cat "$evidence/results.jsonl"
test ! -s "$evidence/runtime.stderr"
test "$(stat -c '%a' "$parent/private")" = 700
for name in capture-1.png capture-2.png; do
    test "$(stat -c '%a' "$parent/private/$name")" = 600
done
stat -c '%a %n' "$parent/private" "$parent/private/capture-1.png" "$parent/private/capture-2.png" > "$evidence/modes.txt"
