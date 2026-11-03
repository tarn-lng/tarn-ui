#!/usr/bin/env bash
# Runs the Tarn GTK backend's tests (backends/linux/gtk/*_test.tarn): the
# devtools protocol unit tests and the native smokes, one process each. GTK
# tests skip without a display. Links come from backends/linux/tarn.toml.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root/backends/linux"
G_DEBUG=fatal-criticals exec "${TARN_BIN:-tarn}" test --timeout 20000 "$@"
