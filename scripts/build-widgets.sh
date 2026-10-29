#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# == 0 || ( $# == 1 && $1 == --dev ) ]] || { echo 'Usage: build-widgets.sh [--dev]' >&2; exit 2; }
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
build="$root/.build/widgets"
packages=(gtk4)
flags=()
if [[ ${1:-} == --dev ]]; then
    build="$root/.build/widgets-dev"
    packages+=(json-glib-1.0)
    flags+=(-DTARN_UI_DEVTOOLS)
fi
mkdir -p "$build/source/core"
cp "$root/core/session.tarn" "$root/core/components.tarn" "$build/source/core/"
cp "$root/examples/widgets/main.tarn" "$build/source/main.tarn"
source "$root/scripts/backend.sh"
ui_backend "$build" "${flags[@]}"
if ! LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" "${UI_LINKS[@]}" \
    -o "$build/widgets" --json > "$build/compiler.jsonl"; then cat "$build/compiler.jsonl" >&2; exit 1; fi
if rg -q '"severity":"warning"' "$build/compiler.jsonl"; then cat "$build/compiler.jsonl" >&2; exit 1; fi
printf 'Built %s/widgets\n' "$build"
