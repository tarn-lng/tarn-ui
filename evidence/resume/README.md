# Resumed validation — 2026-10-20

The first resumed `bash scripts/baseline.sh` exited 101. Local HTTPS package
tests passed; the previous TCP restriction did not reproduce. The run reached
the formatter corpus test, which failed with ENOENT. See
`baseline-stale-cache.log`.

`strings .build/tarn/debug/deps/format-0f8a735630bda629` showed the removed
`tarn-ui/vendor/tarn/tools/fmt` path embedded by `CARGO_MANIFEST_DIR`. Reusing
that build directory after relocating the source produced a stale test binary.
No compiler or test source was changed.

A complete validation was started with a fresh target directory, then interrupted
with Ctrl-C (exit 130) at the user's request to avoid long runs and high CPU use.
Its partial output is `fresh/baseline.log`; it is not a successful full baseline.

Only the failing test was then rerun from the fresh build, with one Cargo job,
one test thread and lower scheduling priority:

```bash
CARGO_TARGET_DIR="$PWD/.build/verified-da3173c" nice -n 10 cargo test \
  --locked --offline --jobs 1 --manifest-path ../tarn/Cargo.toml \
  -p tarn_fmt --test format \
  repository_syntax_corpus_round_trips_and_is_idempotent -- --test-threads=1
```

Exit 0: one passed, five filtered out, 0.66 seconds. See `formatter-fresh.log`.
This confirms the path diagnosis, not a complete suite pass.

The existing pinned compiler also passed safety, borrow-effects and positive
scoped-session probes; summaries are in this directory. `baseline.sh` and
`validate-local.sh` now respect an explicit `CARGO_TARGET_DIR`, and the latter
also respects `EVIDENCE_DIR`. Both scripts passed `bash -n`.

Do not repeat the full suite without a concrete need. Prefer focused checks;
the native mutation test creates its own workers and is not constrained by
Cargo's `--jobs` or the Rust test thread count. Phase 1 remains pending under
the ADR's full-baseline gate.
