#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bash "$root/scripts/checkout-tarn.sh"
bash "$root/scripts/check-environment.sh"
export CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$root/.build/tarn}"
cd "${TARN_SOURCE:-$root/../tarn}"
# Locked dependency graph; fetch separately for the first online setup.
cargo test --locked --offline
