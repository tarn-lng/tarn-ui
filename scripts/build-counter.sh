#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# == 0 || ( $# == 1 && $1 == --dev ) ]] || { echo 'Usage: build-counter.sh [--dev]' >&2; exit 2; }
build="$root/.build/counter"
flags=()
packages=(gtk4)
if [[ ${1:-} == --dev ]]; then
    build="$root/.build/counter-dev"
    flags=(-DTARN_UI_DEVTOOLS)
fi
build="${TARN_UI_BUILD_DIR:-$build}"
[[ $build == /* ]] || { echo 'TARN_UI_BUILD_DIR must be absolute' >&2; exit 2; }
compiler="${TARN_BIN:-$root/.build/verified-da3173c/debug/tarn}"
bash "$root/scripts/checkout-tarn.sh"
mkdir -p "$build/source/core"
cp "$root/core/session.tarn" "$root/core/reactive.tarn" "$build/source/core/"
cp "$root/examples/counter/main.tarn" "$build/source/main.tarn"
cp "$root/examples/counter/counter.tarn" "$build/source/counter.tarn"
source "$root/scripts/backend.sh"
ui_backend "$build" "${flags[@]}"
LIBRARY_PATH="$build" "$compiler" build "$build/source/main.tarn" \
    "${UI_LINKS[@]}" -o "$build/counter" --json > "$build/compiler.jsonl"
{
    "$compiler" --version
    pkg-config --modversion "${packages[@]}"
    sha256sum "$build"/source/gtk/*.tarn "$root/core/session.tarn" "$root/core/reactive.tarn" "$compiler"
    sha256sum "$root/examples/counter/counter.tarn" "$root/examples/counter/main.tarn"
    printf 'ABI=14\n'
} > "$build/provenance.txt"
printf 'Built %s\n' "$build/counter"
if [[ ${1:-} == --dev ]]; then
    printf 'Build launcher: bash scripts/build-launcher.sh; then .build/launcher/tarn-ui --allow inspect click close --allow-text\n'
else
    printf 'Run: bash scripts/run-counter.sh\n'
fi
