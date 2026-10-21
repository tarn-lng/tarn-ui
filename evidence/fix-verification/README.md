# User-provided compiler correction verification

Source base: `2a2e39e8638db219c214bfecfb7de9febed0963c` plus the user's
uncommitted stored-loan-effects correction in the sibling repository.
No compiler files were changed by this verification.
`source-status.txt`, `compiler-working-tree.patch` and `source-hashes.json` record
the source identity, including untracked regression fixtures. The patch alone
omits untracked files; hashes identify them without claiming a committed pin.

The installed compiler rejects all six negative cases in the UI probe scripts;
the overlap between scripts includes the same callback/session defect.
The valid scalar callback and positive scoped-session success/error paths run
successfully. Diagnostic details are saved alongside the summary logs.

Baseline command (does not require a clean upstream worktree):

```bash
CARGO_TARGET_DIR="$PWD/.build/fixed-tarn" cargo test --locked --offline --manifest-path ../tarn/Cargo.toml
```

The normal checkout verification script intentionally still rejects a dirty
worktree; it has not been relaxed to label this uncommitted correction as the
original clean pinned compiler.

## Final results

- Installed and freshly rebuilt compilers: both probe scripts exit 0; positive
  session success/error cleanup probe exits 0.
- `cargo test ... -p tarn_driver --test borrows_suites`: exit 0; both pass/fail
  suite harnesses pass, including the user-added stored-loan fixtures.
- Full `cargo test --locked --offline`: exit 101, 21 passes and one local HTTPS
  registry failure before Cargo stops. curl status 7 at packages.rs:374.
  This is not a full green baseline; Phase 1 remains unstarted.

No compiler changes or commits were made during verification.
