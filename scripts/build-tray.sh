#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
compiler="${TARN_BIN:-$root/../tarn/target/debug/tarn}"
dbus="${TARN_DBUS:-$root/../libs/dbus}"
build="$root/.build/tray"
packages=(gtk4)
flags=()
[[ $# == 0 || ( $# == 1 && $1 == --dev ) ]] || { echo 'Usage: build-tray.sh [--dev]' >&2; exit 2; }
if [[ ${1:-} == --dev ]]; then
    build="$root/.build/tray-dev"
    packages+=(json-glib-1.0)
    flags+=(-DTARN_UI_DEVTOOLS)
fi
rm -rf "$build/source"
mkdir -p "$build/source/core" "$build/source/src" "$build/source/desktop"
cp "$root/core/session.tarn" "$root/core/components.tarn" "$root/core/desktop.tarn" "$build/source/core/"
cp "$dbus"/src/*.tarn "$build/source/src/"
cp "$dbus"/desktop/*.tarn "$build/source/desktop/"
cp "$root/examples/tray/main.tarn" "$build/source/main.tarn"
source "$root/scripts/backend.sh"
ui_backend "$build" "${flags[@]}"
if ! LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" "${UI_LINKS[@]}" \
    -o "$build/tray" --json > "$build/compiler.jsonl"; then cat "$build/compiler.jsonl" >&2; exit 1; fi
if rg -q '"severity":"warning"' "$build/compiler.jsonl"; then cat "$build/compiler.jsonl" >&2; exit 1; fi
printf 'Built %s/tray\n' "$build"
