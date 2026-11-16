#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/cairn_b5_onboarding"
[[ -x "$build/cairn_b5_onboarding" ]] || {
    echo 'Build first: nice -n 10 bash scripts/build-cairn-b5.sh' >&2
    exit 1
}
exec "$build/cairn_b5_onboarding" "$@"
