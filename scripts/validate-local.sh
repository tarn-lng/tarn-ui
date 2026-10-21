#!/usr/bin/env bash
# Run in the host's terminal. No installation, source edits or test exclusions.
set -uo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bash "$root/scripts/checkout-tarn.sh" || exit "$?"
bash "$root/scripts/check-environment.sh" || exit "$?"
export CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$root/.build/tarn}"
export TARN_BIN="$CARGO_TARGET_DIR/debug/tarn"
export EVIDENCE_DIR="${EVIDENCE_DIR:-$root/evidence/local-validation}"
mkdir -p "$EVIDENCE_DIR"
result=0
cargo test --locked --offline --no-fail-fast --manifest-path "${TARN_SOURCE:-$root/../tarn}/Cargo.toml" > "$EVIDENCE_DIR/baseline.log" 2>&1
status=$?
printf '%s\n' "$status" > "$EVIDENCE_DIR/baseline.exit"
printf 'Full baseline: exit %s\n' "$status"
(( status == 0 )) || result=1
# Ensure the tested revision's compiler exists even if compilation failed earlier.
cargo build --locked --offline --manifest-path "${TARN_SOURCE:-$root/../tarn}/Cargo.toml" -p tarn > "$EVIDENCE_DIR/compiler-build.log" 2>&1 || exit 1
for script in safety-probes borrow-effects-probes scoped-probe; do
    bash "$root/scripts/$script.sh" > "$EVIDENCE_DIR/$script.log" 2>&1
    status=$?
    printf '%s\n' "$status" > "$EVIDENCE_DIR/$script.exit"
    printf '%s: exit %s\n' "$script" "$status"
    (( status == 0 )) || result=1
done
printf 'Logs: %s\n' "$EVIDENCE_DIR"
exit "$result"
