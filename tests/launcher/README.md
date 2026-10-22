# Native Tarn launcher tests

Run the focused executable suite after building the GTK development example:

```bash
bash scripts/test-launcher.sh
```

`TARN_BIN` selects a compiler; the default is `../tarn/target/debug/tarn`.
The script rejects warnings, preserves diagnostics, and builds/runs ordinary Tarn
executables. Its shell code only orchestrates compilation and tests. Launcher
and fixture process creation does not use shell, Python, extra FFI or a C listener.
A missing GTK display or binary is a failure, not a passing skip.

## Positive ownership probes

`recursive-error.tarn` returns an owned recursive error from a callback.
`scalar-error-control.tarn` changes only its payload to i32. Both must compile
and execute with stdout `false`. The recursive case previously exposed E4201 and
E4102 and is now accepted by compiler `39c4617` (ADR 0074). It is a positive
regression probe, never a negative test asserting those false rejections.

## Public process/channel contract: four checks

`process-contract.tarn` checks real GTK socketpair startup/authorization/close,
early EOF with exit 23, private captured stdout/stderr, timeout/KILL/wait and
TERM/wait. Its child fixtures are the same executable launched in child modes.

## Raw launcher acceptance: three checks

`acceptance.tarn` invokes the actual native launcher and verifies native ready
and close records, early EOF/status diagnostics and private output, and a silent
child's channel timeout with explicit TERM/wait and forced-cleanup metadata.
`fixture.tarn` supplies early-exit and unresponsive child behavior. The driver's
own observation is bounded and explicitly kills before reaping on failure.
Inputs and expected outputs fit the pipes before that observation.

## MCP stdio acceptance: four checks

`mcp-acceptance.tarn` is an independent MCP stdio client talking to the real Tarn
executable. It checks:

- Initialization order, IDs, malformed/duplicate JSON, unknown fields, ignored
  notifications, discovery and unauthorized tool calls.
- Real inspect/click/reinspect, acknowledged counter update, actual PNG image
  response at the revision, native close and a subsequent ping.
- Early child EOF/status and timeout/forced-cleanup as structured MCP tool errors.
- Restricted grants hide and reject click, screenshots and diagnostics.

Every stdout line is parsed as JSON-RPC. Extra buffered/output bytes and child
markers fail the checks. Screenshot checks include native dimensions/revision,
PNG base64 signature and matching MCP image data, not an independent SHA-256
recomputation. These tests do not replace the separate render sanitizer checks.

The previous Python launcher is retained only as `reference-launcher.py` and the
protocol harness helper in `../devtools/reference_session.py`. The public native
entry point is `.build/launcher/tarn-ui`. See the [migration report](../../docs/launcher-migration.md)
for resource/deadline mapping and actual results. Generated logs, compiler JSON,
exit records and stderr are retained under `.build/launcher-tests/`.

## Optional MCP project build

Run `bash scripts/test-mcp-build.sh` with a GTK display. It runs two direct build
checks and six MCP cases, including the four existing cases. The new cases verify
authorized discovery with an empty argument schema, rejection of supplied command
arguments, real compilation followed by starting the generated binary, refusal
to build during an active UI session, and structured E2001 propagation without
losing the previously selected executable. Build subprocess output must remain
inside the MCP result. Evidence is stored in `.build/build-tests/` and
`.build/launcher-tests/mcp-build.{jsonl,stderr}`. Shell scripts only stage and run
tests; `project.build` itself uses ordinary Tarn process APIs directly.

The build acceptance now saves an actual screenshot into a fresh private
directory, recomputes its SHA-256 and compares all bytes with the MCP payload.
The harness checks 0700/0600 modes and records an external sha256sum. It writes
`saved-capture.path` for the following focused contracts:

```bash
bash scripts/test-artifacts.sh
bash scripts/test-private-creation.sh
```

The artifact contract has 19 executable checks: valid PNG, corrupted encoding,
digest, signature and CRC; incorrect dimensions/session/revision; truncation,
trailing bytes and decode budget; invalid input creates no directory; repeated
saves use distinct paths; persistence is opt-in; existing directories/files are
refused and filesystem errors propagate. The permission probe uses public Tarn
APIs with test umask 022. Neither script runs Cargo or GUI rendering. The existing
native protocol suite separately covers capture failure and close while pending.
