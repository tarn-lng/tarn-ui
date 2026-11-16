#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/cairn_b1_rects"
[[ -x "$build/cairn_b1_rects" ]] || {
    echo 'Build first: nice -n 10 bash scripts/build-cairn-b1.sh' >&2
    exit 1
}
exec "$build/cairn_b1_rects" "$@"
