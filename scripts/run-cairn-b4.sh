#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
name=${1:-widgets}
if [[ $# -gt 0 ]]; then shift; fi
build="$root/.build/cairn_b4_$name"
[[ -x "$build/$name" ]] || {
    echo "Build first: nice -n 10 bash scripts/build-cairn-b4.sh $name" >&2
    exit 1
}
exec "$build/$name" "$@"
