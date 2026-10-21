#!/usr/bin/env bash
# Build-time orchestration only. The executable launches children in Tarn.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
build="$root/.build/launcher"
mkdir -p "$build"
if ! "$compiler" build "$root/devtools/main.tarn" -o "$build/tarn-ui" --json > "$build/compiler.jsonl"; then
    cat "$build/compiler.jsonl" >&2
    exit 1
fi
if rg -q '"severity":"warning"' "$build/compiler.jsonl"; then
    cat "$build/compiler.jsonl" >&2
    exit 1
fi
{
    "$compiler" version
    sha256sum "$compiler" "$root/devtools/main.tarn" "$root/devtools/session.tarn" "$root/devtools/wire.tarn" "$root/devtools/mcp.tarn"
    uname -sr
} > "$build/provenance.txt"
printf 'Built %s\n' "$build/tarn-ui"
