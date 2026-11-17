#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/cairn_compositor"
[[ -x "$build/cairn_compositor" ]] || {
    echo 'Build first: nice -n 10 bash scripts/build-cairn-compositor.sh' >&2
    exit 1
}
exec "$build/cairn_compositor" "$@"
