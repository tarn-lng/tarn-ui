# Native heading window

Historical Phase 1 record. The active example and scripts were extended and
renamed for Phase 2; use [the current counter guide](counter.md). Phase 1 results
below describe the earlier ABI and widget tree, not the current implementation.

The Phase 1 implementation opens a GTK4 window titled **Tarn UI** with the
native label **Hola desde Tarn**. The example runs on the Linux process main
thread, pulls `Opened`/`Closed` value events and releases the session before
returning. The user observed the window and closed it; the process exited 0.

```bash
bash scripts/build-counter.sh
bash scripts/run-counter.sh
```

Requirements: Linux x86_64, GTK4 development/runtime packages, system C compiler,
pkg-config, a working desktop display, and the previously built pinned Tarn
compiler at `.build/verified-da3173c/debug/tarn`. Set `TARN_BIN` to select another
compiler deliberately; record its provenance separately.

The build script verifies the clean sibling Tarn checkout, compiles the bridge
as a shared library, then builds the Tarn example with `--link tarn_ui_gtk`.
It stages copies of the two Tarn source files in `.build/heading/source`, since
Tarn local imports resolve from the entry directory and reject `..` segments.
No package manifest, compiler extension or install hook is involved.
`LIBRARY_PATH` and runtime `LD_LIBRARY_PATH` contain only the absolute build
directory. Compiler JSON diagnostics and build hashes are saved there.

## Contract and ownership

`core/session.tarn` exposes owned string configuration, `Error`, `Event`, the
opaque non-Copy `Session`, `run`, `next_event` and `request_close`. Native types,
FFI declarations, construction and release remain private. This prototype
selects GTK internally; only its public semantic interface is portable.

`run` is unsafe because callers must enter from the process main thread without
a nested session. The C adapter also checks Linux `gettid() == getpid()` before
any GTK operation and rejects another active session. The session cannot be
transferred to a task through safe Tarn. ABI 1 is checked before creation.
Title/heading strings reject embedded NUL and remain alive during creation;
GTK copies them synchronously. C retains no Tarn pointer or closure.

`gtk_window_new` returns a borrowed pointer owned by GTK's toplevel list. The
bridge takes one explicit reference. `gtk_window_set_child` sinks the label's
floating reference; the parent is its only owner. On cleanup the bridge
disconnects both signals, destroys the window (dropping GTK's toplevel reference),
drops its own reference, then frees session storage. All partial creation
failures use that cleanup path. Source ownership was checked against the
installed GTK 4.24.1 GIR documentation.

The map and close-request signals invoke C handlers only. They set two bounded
event flags. A close request hides the window and supersedes an unconsumed
Opened event; destruction waits for the scoped runner. Closed is delivered once,
and later operations return a closed error. `next_event` blocks in GLib context
iteration instead of polling. No per-session GLib source is installed.

The runner stores the body result, releases the native session, then returns the
result. An error propagated with `try` inside the body follows this path too.
Process abort, explicit process exit and forced termination do not unwind.

## Focused verification

```bash
# Compile only: no window, full suite or Cargo test run.
bash scripts/test-gtk.sh --build-only
# Opens and automatically closes brief test windows; retains sanitizer failures.
bash scripts/test-gtk.sh
```

Tests cover actual native label/title getters and rendering; repeated lifecycle;
three partial-create failures; wrong-thread admission/operations; nested session
rejection; close priority and repeated close handling; actual Tarn normal close,
error propagation and early return; and compile-fail probes against the real
wrapper for private construction, task transfer and borrow escape.

The screenshot comes from the realized GTK widget through its actual GSK renderer
after a frame, not from generated imagery. Tests use a watchdog deadline rather
than a settlement sleep. Production builds export only the five bridge ABI
functions; test hooks, fault injection and allocation observers compile only
under `TARN_UI_TESTING`.

See [recorded results](../evidence/heading/README.md). Functional tests and
per-session accounting pass. LeakSanitizer exits 1 for identical Fontconfig/Mesa
allocations in both the bridge run and the GTK-only control. No leak checks were
disabled or suppressed. The aggregate test command therefore still exits 1;
do not describe it as a completely green sanitizer run.

This implementation was explicitly authorized after the user requested focused
checks instead of another CPU-heavy full baseline. The full baseline remains
unverified; that original ADR completion gate has not been claimed as passed.
Counter behavior and later phases remain deferred.
