# Phase 1 targeted evidence — 2026-10-20

Implementation and reproduction: [native heading guide](../../docs/native-ui.md).
Tarn source pin: `da3173c4f012ead629aa03e67d3d9147615fec62` (clean sibling checkout).
GTK 4.24.1; GCC 16.2.1; native Linux desktop display. The existing fresh compiler
was used; no complete suite was rerun for this implementation.

## Results

- Production example built successfully with empty compiler diagnostics.
- User confirmed seeing the interactive window. Its log is `interactive.log`
  (`opened`, `closed`); process exit was 0.
- C lifecycle assertions all passed: 19/19 session allocations/frees,
  18/18 windows finalized, 17/17 headings finalized, 32/32 signal connections
  disconnected. Counts include injected partial failures and 16 complete cycles.
- Native window capture: `window.png`, 480 × 240; inspected visually.
- Tarn integration: exit 0, `normal-close`, `early-return`, `clean`.
  The same test also verifies preservation of body error code 99 and checks
  allocation/finalization/handler balance after every run.
- Real-wrapper negative probes rejected with E3014 (private construction),
  E3047 (task transfer), and E4210 (borrow escape); JSON records retained.
- Production dynamic symbols contain only the five `tarn_ui_gtk_*` ABI functions.

## Sanitizer limitation, retained as a failing check

`bridge.log` and `control.log` both end with:

```text
SUMMARY: AddressSanitizer: 624792 byte(s) leaked in 13564 allocation(s).
```

Both runs use ASan/UBSan with LeakSanitizer enabled. The control creates, renders
and destroys the same GTK widgets without any bridge sessions or handlers. The
reported allocation stacks originate in Fontconfig and Mesa/EGL in both runs;
no additional leaked bytes/allocations were observed with the bridge. This
supports dependency/process-global retention as the explanation for this run,
while the weak-finalization and accounting observations independently establish
per-session cleanup. It is not a blanket proof that all native memory is freed.

Both native processes exit 1 because of LSan; the combined test command retains
that exit 1 even though the assertions and Tarn smoke pass. No suppressions,
disabled leak detection or source changes to GTK/Tarn were used.

`runtime.log`, separate `.exit` files, compiler JSON, source/toolchain hashes and
the PNG hashes are retained alongside this record. Strict original full-baseline
and completely clean process-wide leak gates remain outstanding. The Phase 1
implementation and focused functional verification are complete; later phases
have not been started.
