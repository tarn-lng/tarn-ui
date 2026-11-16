#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/cairn_b2_welcome"
[[ -x "$build/cairn_b2_welcome" ]] || {
    echo 'Build first: nice -n 10 bash scripts/build-cairn-b2.sh' >&2
    exit 1
}
exec "$build/cairn_b2_welcome" "$@"
