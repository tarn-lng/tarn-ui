#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
evidence="$root/.build/launcher-tests"
bash "$root/scripts/build-launcher.sh"
bash "$root/scripts/test-project-build.sh"
mkdir -p "$evidence"
for name in mcp-acceptance fixture; do
    "$compiler" build "$root/tests/launcher/$name.tarn" -o "$evidence/$name" --json > "$evidence/$name.compiler.jsonl"
    if rg -q '"severity":"warning"' "$evidence/$name.compiler.jsonl"; then
        cat "$evidence/$name.compiler.jsonl" >&2
        exit 1
    fi
done
read -r invalid_root < "$root/.build/build-tests/invalid-root.txt"
capture_parent=$(mktemp -d "$evidence/captures.XXXXXXXX")
export TARN_UI_TEST_CAPTURE_DIR="$capture_parent/private"
"$evidence/mcp-acceptance" "$root/.build/launcher/tarn-ui" "$root/.build/counter-dev/counter" "$root/.build/counter-dev" "$evidence/fixture" "$root" "$compiler" "$invalid_root" > "$evidence/mcp-build.jsonl" 2> "$evidence/mcp-build.stderr"
cat "$evidence/mcp-build.jsonl"
test ! -s "$evidence/mcp-build.stderr"
test "$(stat -c '%a' "$TARN_UI_TEST_CAPTURE_DIR")" = 700
test "$(stat -c '%a' "$TARN_UI_TEST_CAPTURE_DIR/capture-1.png")" = 600
sha256sum "$TARN_UI_TEST_CAPTURE_DIR/capture-1.png" > "$evidence/saved-capture.sha256"
printf '%s\n' "$TARN_UI_TEST_CAPTURE_DIR/capture-1.png" > "$evidence/saved-capture.path"
