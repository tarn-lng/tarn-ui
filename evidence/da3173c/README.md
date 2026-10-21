# Verification at clean da3173c

Verified sibling `../tarn` is clean and complete at
`da3173c4f012ead629aa03e67d3d9147615fec62`, including the borrow correction and
HTTPS fixture address correction. `scripts/checkout-tarn.sh` now pins this commit.
No checkout/source changes or commits were made in Tarn.

Commands run from tarn-ui:

```bash
bash scripts/baseline.sh
TARN_BIN="$PWD/.build/tarn/debug/tarn" EVIDENCE_DIR="$PWD/evidence/da3173c" bash scripts/safety-probes.sh
TARN_BIN="$PWD/.build/tarn/debug/tarn" EVIDENCE_DIR="$PWD/evidence/da3173c" bash scripts/borrow-effects-probes.sh
TARN_BIN="$PWD/.build/tarn/debug/tarn" bash scripts/scoped-probe.sh
```

Both negative probe scripts and the positive scoped-session probe exit 0 with
the compiler built from the new clean source revision. Invalid programs were
only compiled to verify rejection, never executed. The owned scalar callback
and positive session normal/error paths were executed successfully.

The full locked offline baseline exits 101: 21 passes, one failure, before Cargo
stops. The HTTPS package test now uses 127.0.0.1 but still cannot connect here.
This is not evidence of a Tarn defect: an independent Python HTTP server cannot
even bind to `127.0.0.1` in this executor (`PermissionError: [Errno 1] Operation
not permitted`). See `loopback-check.log`. The fixture cannot start its local
TCP server under this sandbox restriction. No TLS checks were disabled.

The user's successful seven-package-tests run occurred outside this restricted
execution; the two observations describe different execution permissions.
This environment cannot pass the required full-suite gate until local TCP
binding/connections are permitted by its configuration. That configuration is
outside this workspace; no compiler or test modification is warranted here.
Phase 1 remains unstarted, as the full baseline requirement has not passed here.
All earlier evidence directories describe earlier source states and are retained
as historical records.
