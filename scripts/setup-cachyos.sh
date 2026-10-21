#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bash "$root/scripts/checkout-tarn.sh"
bash "$root/scripts/check-environment.sh"
command -v tarn
tarn --version
export CARGO_TARGET_DIR="$root/.build/tarn"
cargo fetch --locked --offline --manifest-path "${TARN_SOURCE:-$root/../tarn}/Cargo.toml"
printf 'Ready. Run bash scripts/baseline.sh and bash scripts/scoped-probe.sh\n'
