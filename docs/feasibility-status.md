# Feasibility gate: full baseline pending

Latest authorized milestone: Phase 3 development control. See
[actual Phase 3 results](phase3-results.md); this does not claim a green full
baseline or resolution of the renderer's LeakSanitizer findings.

Current status: the user accepted Phase 1 and authorized Phase 2 only. The native
counter and targeted tests are recorded in [Phase 2 results](phase2-results.md).
The earlier gate observations below are historical and do not supersede that
authorization. No full-suite or completely clean LSan pass is claimed.

Phase 1 was subsequently authorized with focused checks and is now implemented:
a native heading window, scoped Tarn runner and deterministic per-session
cleanup. The user observed the example; normal interactive shutdown exited 0.
Native and Tarn functional assertions pass. LSan reports identical outstanding
Fontconfig/Mesa allocations with and without the bridge, so the aggregate native
check retains failure. No full-baseline pass is claimed. See
[Phase 1 results](../evidence/heading/README.md).

Latest resumed validation: local HTTPS tests pass. The baseline reached a
formatter corpus test whose cached binary retained the deleted `vendor/tarn`
path. That test passes after a fresh build. The fresh full run was interrupted
at the user's request to avoid lengthy CPU-heavy testing; it is not a full pass.
Prefer focused checks and do not repeat the entire suite without a concrete
need. Phase 1 remains pending. See [latest evidence](../evidence/resume/README.md).

## Historical TCP restriction

The sibling `../tarn` is clean at **da3173c4f012ead629aa03e67d3d9147615fec62**,
now pinned by the checkout verification script. It includes the stored-loan fix
and HTTPS fixture correction. Both negative probe scripts and the positive scoped
session probe pass with the compiler rebuilt from this revision.

The full baseline still exits 101 on the HTTPS registry fixture (curl code 7).
An independent Python HTTP server fails to bind 127.0.0.1 with
`PermissionError(1, Operation not permitted)`, confirming a local TCP restriction
in this executor. The fix belongs in execution permissions, not in Tarn.

Phase 1 remains unstarted because the required full baseline cannot finish here.
No compiler/test edits, safety exceptions, TCP workarounds or commits were made.

See [current commands and evidence](../evidence/da3173c/README.md).
