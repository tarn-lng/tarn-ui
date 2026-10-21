#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/.build/counter"
[[ -x "$build/counter" && -f "$build/libtarn_ui_gtk.so" ]] || {
    echo 'Build first: bash scripts/build-counter.sh' >&2
    exit 1
}
export LD_LIBRARY_PATH="$build"
exec "$build/counter"
