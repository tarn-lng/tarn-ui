Current status: the original defect is fixed in bde1757, included in the clean
pinned da3173c revision. Both negative probe scripts and the positive session
probe pass. [Current verification](../evidence/da3173c/README.md).
The investigation below is retained as historical evidence.

# Scoped-session containment: original failure and correction verification

Latest check: the user-provided correction is present as uncommitted changes in
`../tarn`. The installed compiler now rejects all negative reproductions, and
both positive probes pass. The following sections describe the original failure.
See `evidence/fix-verification/` for current source hashes and execution logs.

Original revision: `2a2e39e8638db219c214bfecfb7de9febed0963c`.
The compiler and safety rules were not modified. Phase 1 has not started.

## Reproduction

Use the pinned compiler built by the baseline, independently of the installed
binary's version identity:

```bash
TARN_BIN="$PWD/.build/tarn/debug/tarn" bash scripts/safety-probes.sh
```

Observed exit: **1**, deliberately failing the gate:

```text
PASS: private-construction rejected
PASS: task-transfer rejected
FAIL: borrow-escape was accepted; binary was NOT executed
```

`tests/compiler/scoped-session/borrow-escape.tarn` captures an outer
`Option<&mut session.Session>` in the local mutable callable, assigns its session
parameter into that outer storage, then uses the reference after `session.run`
returns. The runner's local Session has gone out of scope and its native
allocation has already been freed. This source should be rejected for the proposed
scoped API to establish containment. Both the installed compiler and the compiler
built from the exact pin accepted it (build exit 0, no diagnostic).
The binaries were **not executed**. No runtime crash or exploit is claimed.

The ordinary positive probe still compiles and runs with the pinned compiler,
including cleanup after an error returned from the body. That positive result
does not establish that the caller cannot retain a session-derived reference.
Private field construction is rejected with E3014 and task transfer with E3047.

## Consequence and proposed next investigation

Do not implement the GTK wrapper using this shape yet. A general-purpose compiler
investigation should track borrowed callback parameters assigned into captured
storage and preserve their provenance across mutable callable invocation. The
required semantic result is rejection when a reference to the runner's local
value escapes its lifetime. This is a candidate direction, not a diagnosed
implementation root cause or a compiler patch.

Any future fix needs a general regression derived from this reproducer, plus
positive coverage for borrowed callable parameters used only during invocation.
Do not grant UI-specific trust, erase the reference to a transferable integer,
relax borrow checks, or substitute a runtime liveness table for the safety gate.
Compiler work requires separately scoped authorization; none was performed here.

## Independent environmental blocker

The full baseline was rerun against `../tarn` with the locked offline graph:
exit 101, local HTTPS registry test failed with curl status 7 at
`tools/cli/tests/packages.rs:374`. Cargo stopped before the complete suite finished.
The executed summaries contain 21 passes and one failure. An additional locked
fetch failed with exit 101 because `static.crates.io` could not resolve. Missing
optional/target dependency archives do not contradict a host build using cached
crates. No test was skipped or altered to make this pass.

See `evidence/baseline-current.log`, `evidence/cargo-fetch.log`,
`evidence/safety-probes-pinned.log`, `evidence/safety-*.log`, and
`evidence/scoped-session-pinned.log`. Earlier logs are retained as history.

## Follow-up: reduced to ordinary safe Tarn

The same problem reproduces without UI, raw pointers, FFI or `unsafe`.
Files in `tests/compiler/borrow-effects/` isolate the effect:

| Probe | Expected | Pinned compiler observed |
| --- | --- | --- |
| `callback-escape.tarn` | Reject retained reference to scoped local | Build exit 0; not run |
| `direct-store-escape.tarn` | Reject storing scoped local via helper into caller storage | Build exit 0; not run |
| `local-store-control.tarn` | Reject directly storing scoped local into caller storage | E4207, exit 1 |
| `callback-valid.tarn` | Allow copying scalar value during callback | Build/run exit 0, prints 42 |

Reproduce the matrix (expected current exit **1**, because two negative tests
are accepted):

```bash
TARN_BIN="$PWD/.build/tarn/debug/tarn" bash scripts/borrow-effects-probes.sh
```

Only the valid control is executed. Acceptance of the other sources is the
observed defect; no runtime misbehavior has been measured.

### Source-level explanation

At the pinned revision, `compiler/ownership/src/borrows.rs` represents function
provenance as argument positions flowing into the **result** (`Provenance`, near
line 49; `summary`, near line 582). Retained stores are handled separately:

- `write_holds` near line 392 does not record assignments through a dereference.
- `check_stmt` near line 661 rejects direct stores of loans rooted in the current
  function's own locals. Parameter placeholder loans are excluded. This explains
  the difference between directly storing `&local` and passing it to `store`.
- `store_effects` near line 508 conservatively unions argument loans into mutable
  storage targets, but only for loans satisfying `loan.holder == arg.local` and
  `loan.param.is_none()`. In the helper case the relevant write is through a
  reborrow of caller storage; retaining its own local loan on a temporary argument
  does not establish the escaping write to that caller storage.
- `store_effects` scans explicit call arguments, not `Callee::Value`'s captured
  environment as a destination for borrowed arguments. In the callback IR,
  `scoped` invokes the callable with `&local`; the destination `saved` is hidden
  in the callable environment. Result-only provenance cannot express this write.

Saved IR (`evidence/callback-escape.ir` and `direct-store-escape.ir`) confirms:
`local` reaches `dead` after the invocation; `main` later reads through the saved
reference. These are concrete gaps in the paths exercised by the reproductions,
not a complete audit of all stored-reference behavior.

### Smallest defensible correction direction

A generic ownership fix must account for loans retained through mutable arguments
and callable environments, including reborrows of caller-owned storage. Either
propagate these effects through function summaries and call sites, or reject
unmodeled retained-loan boundaries conservatively with a clear diagnostic.
Do not infer the absence of stored loans from a void/owned return value.

An initial conservative restriction may reject useful callbacks. Preserve the
valid scalar-copy control and measure compatibility with the full baseline before
selecting that restriction. More precise summaries should distinguish reading a
borrow during a call from storing it past the call, without inventing UI-specific
lifetime rules or changing the backend to decide borrow semantics.

Implementation acceptance must include the original Session case, both pure Tarn
negative cases, valid local mutable callbacks, caller-owned loans with valid
lifetimes, reborrow/nested helper coverage and recursive summary convergence.
The existing complete baseline must pass in a suitable environment. No compiler
patch was made: the original instruction prohibiting compiler changes remains in
force. This investigation provides the reproductions and proposed scope for a
separately authorized compiler correction.
