#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bash "$root/scripts/checkout-tarn.sh"
mkdir -p "$root/.build"
compiler="${TARN_BIN:-tarn}"
command -v "$compiler"
# Version output alone does not prove the installed binary's source revision.
"$compiler" --version
"$compiler" build "$root/tests/compiler/scoped-session/main.tarn" -o "$root/.build/scoped-session"
actual=$("$root/.build/scoped-session")
expected=$'true\nclosed\nok\nclosed\n7\n1'
[[ "$actual" == "$expected" ]]
printf '%s\n' "$actual"
