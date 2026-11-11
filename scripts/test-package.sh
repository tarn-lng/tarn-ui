#!/usr/bin/env bash
# Publishes tarn_ui to a throwaway registry and builds a fresh consumer
# project from it with plain `tarn add` + `tarn build` (no copied sources).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-$(command -v tarn)}"
work=$(mktemp -d "${TMPDIR:-/tmp}/tarn-ui-package.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
bash "$root/scripts/package.sh" "$work/registry" >/dev/null
mkdir "$work/app"
cp "$root/examples/consumer/main.tarn" "$root/examples/consumer/counter.tarn" "$work/app/"
sed '/^dependencies/d' "$root/examples/consumer/tarn.toml" > "$work/app/tarn.toml"
cd "$work/app"
version=$(/usr/bin/python3 -c 'import sys,tomllib; print(tomllib.load(open(sys.argv[1], "rb"))["package"]["version"])' "$root/package/tarn.toml")
"$compiler" add tarn_ui --version "^$version" --registry "file://$work/registry/" >/dev/null
"$compiler" build main.tarn -o "$work/app/consumer" --json > "$work/compiler.jsonl"
! grep -q '"severity":"warning"' "$work/compiler.jsonl" || { cat "$work/compiler.jsonl" >&2; exit 1; }
test -x "$work/app/consumer"
# Release package must not carry the devtools transport.
! grep -rq 'TARN_UI_DEVTOOLS\|socketpair' "$root/.build/package/tarn_ui/gtk/devtools.tarn"
printf 'package check passed: published, locked and built a consumer\n'
