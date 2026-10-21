# Tarn launcher migration

Status: **the real launcher builds and runs in Tarn**. The public entry point is
`.build/launcher/tarn-ui`, built with `bash scripts/build-launcher.sh`. It supports
the existing raw development protocol and an MCP stdio adapter (`--mode mcp`).
Child launch, socketpair handoff, pipe draining and cleanup use public stdlib APIs.
There is no Python, shell, additional FFI or C listener in that runtime path.

The Python implementation is retained only as a test reference in
`tests/devtools/reference_session.py` and `tests/launcher/reference-launcher.py`.
Unfinished earlier Python Phase 4 prototypes were archived locally under the
ignored `.build/archive/phase4-python/`; they are not production entry points.

## Build and run

```bash
bash scripts/build-launcher.sh
.build/launcher/tarn-ui --allow inspect click close --allow-text
.build/launcher/tarn-ui --mode mcp --allow inspect click close screenshot diagnostics --allow-text --allow-capture
```

The build script defaults to `../tarn/target/debug/tarn`; `TARN_BIN` selects an
explicit compiler. The launcher defaults to `.build/counter-dev/counter` and
accepts `--binary` and `--library-path` at startup. Tool requests cannot change
those paths, execute builds or expand permissions. The GTK development example
must already be built. Its existing backend remains unchanged by this migration.

The build script rejects compiler warnings and records compiler/source hashes in
`.build/launcher/provenance.txt`. These shell scripts orchestrate compilation;
the resulting executable launches its child directly through `process.Command`.

## Resource and failure mapping

| Python reference behavior | Tarn equivalent |
| --- | --- |
| Private `socket.socketpair()` without a filesystem path | `net.UnixStream.pair()` |
| Hand only the chosen endpoint to the child | Consuming `Command.pass(pair.second).start_child()`; child fd 3 |
| Close the parent's handed-over copy, including spawn failure | One-shot Launch ownership, supplied by stdlib |
| Select fd/grants/text/pixels in the child environment | `Command.env` overrides `TARN_UI_DEV_FD=3`, operation grants and opt-in flags |
| Keep native stdout/stderr out of the host protocol | `Stdio.Piped`, owned pipe readers and concurrent nonblocking draining |
| Retain diagnostic bytes | Owned stdout/stderr vectors; raw CLI exposes them only with the diagnostics grant |
| Detect early child death | Channel EOF, `try_wait`/`wait_timeout`, then the unique consuming `wait` |
| Ready followed by session authorization | Parse ready, bind its session ID and send `session.authorize` |
| Correlate launcher requests | Session-specific request ID namespace; preserve raw caller request IDs |
| Preserve native/application errors | Raw native response, or MCP `isError` with structured code/stage/details |
| Distinguish forced cleanup from success | Native exit/signal and explicit `forced_cleanup` metadata |

The scoped runner owns the Process outside fallible protocol and callback helpers.
It saves their result before cleanup. No `try` can drop a potentially live child
past that boundary: cleanup explicitly sends TERM and, if needed, KILL before the
unique reap. Pipe EOF deregisters readiness so retired readers cannot repeatedly
wake Poll. All native GTK operations still happen in the child's UI thread.

## Deadline mapping

| Existing behavior | Tarn implementation |
| --- | --- |
| Five-second socket send timeout | Partial writes and writable Poll under a whole-send monotonic deadline |
| Five-second socket receive timeout | Nonblocking reads and readable Poll; each successful read resets the idle deadline, matching Python `recv` |
| Startup and authorization | The same five-second channel operations; EOF and timeout remain distinct errors |
| Native action completion: two seconds | Existing backend deadline remains authoritative; native `TIMEOUT` is preserved |
| Native close: three seconds to exit | Bounded completion observation while draining both output pipes |
| Fallback cleanup: SIGTERM, then three-second grace | `Process.terminate()` followed by bounded observation/draining |
| Escalation | `Process.kill()` followed by consuming `wait()`; destruction never substitutes for signalling |
| Drain trailing diagnostic output | Bounded draining after child completion, closing remaining pipe owners on return |

Defaults are 5000 ms for channel operations and 3000 ms for close/TERM/output
cleanup. `--io-timeout-ms` and `--close-timeout-ms` allow explicit startup settings
between 1 and 60000 ms. The 2 MiB native response budget remains. Raw stdin and MCP
requests are limited to 1 MiB. Captured diagnostic vectors remain unbounded as in
the Python reference; no truncation policy was silently introduced. Post-KILL
reaping follows the standard library's owned-process contract.

EOF on launcher stdin attempts authorized native close; cleanup requiring signals
is explicitly marked forced. Raw CLI exits 2 for forced cleanup, 1 for other
failures and 0 for successful native shutdown. It reports failures on stderr as
structured JSON with native status. Output records and child diagnostics never
share the protocol stdout stream.

## MCP stdio boundary

The Tarn adapter uses the explicit **2025-11-25** protocol profile, initialization
and initialized notification, JSON-RPC IDs, `tools/list` and `tools/call`. It is a
bounded stdio implementation, not a claim of every MCP transport or SDK feature.
See the pinned [lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle),
[tools](https://modelcontextprotocol.io/specification/2025-11-25/server/tools) and
[stdio](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports) contracts.

Only launcher-granted tools are discoverable. `ui.start` starts the configured
application lazily; inspect/click/close/diagnostics require their operation grants.
Screenshot additionally requires `--allow-capture`. Native text requires
`--allow-text`; pixel access is a separate explicit authorization. Responses carry
`structuredContent`, textual content and `isError`. Screenshots additionally
carry the actual PNG as MCP image content with native revision, dimensions and
SHA-256 metadata. `ui.close` is answered after successful child completion.
There are no shell, build, path-selection, HTTP, resource or prompt tools.

EOF closes an active native session through the same runner. Operation errors
are MCP tool errors, distinct from JSON-RPC parse/method errors. Unknown
notifications produce no responses. Cancellation notifications do not interrupt
an in-flight synchronous operation; native/channel deadlines still apply.

## Current blocker: resolved by ADR 0074

The positive [recursive callback probe](../tests/launcher/recursive-error.tarn)
previously failed with E4201/E4102, despite returning an owned recursive enum with
no references. The compiler correction in `39c4617` makes containment queries
inductive on repeated recursive types (ADR 0074). The same probe now builds
without warnings, without changing its payload to a scalar or string. The real
launcher also builds with its owned `json.Value` errors intact.

The [scalar control](../tests/launcher/scalar-error-control.tarn) remains a
positive control. The initial missing SIGTERM operation was resolved separately
by ADR 0073. Neither issue is a current migration blocker. Earlier rejection
records remain local historical evidence; they are not accepted compile-fail
language contracts.

## Executable verification

The executable tests live in [tests/launcher](../tests/launcher/README.md):

- Public stdlib contract: real GTK socketpair startup/authorization/close, early
  EOF plus exit 23 with separate output capture, timeout/KILL/wait, TERM/wait.
- Black-box raw launcher: native start/close records, early EOF with structured
  exit 23 and no leaked child markers, timeout/TERM/wait with forced metadata.
- Independent Tarn MCP client: initialization, discovery, real native inspection,
  click and acknowledged counter update, real PNG image response and native close;
  early child exit and timeout remain valid MCP responses with private child output.
- Restricted grants: discovery and invocation cannot expose unauthorized tools.

The screenshot test checks PNG base64 signature, dimensions, hash field, image
content equality and revision ordering; it does not independently recompute the
PNG hash or replace visual/render sanitizer testing. Launcher migration does not
claim to resolve the existing GTK/Fontconfig/Mesa LeakSanitizer findings. No
suppressions were added and no compiler/trusted-module changes were made here.

Current logs are retained under `.build/launcher-tests/`. Cargo output is retained
at `.build/evidence/cargo-test-migration.log`; generated evidence stays ignored by
Git. No commit or publication is performed automatically.

## Actual final results on 2026-10-21

Compiler: `39c4617677060a6befce783e000facca79129459`, reporting `tarn 0.0.4`.
Its binary SHA-256 is
`3bb88bb6c695a93868b7afdc843671d461913ca38a13fa415142706923d153b7`.
The sibling workspace contains existing uncommitted driver/resolver changes;
they were preserved. These results exercise that supplied working-tree compiler,
not a claimed clean build of the commit. No compiler source was changed here.

| Check | Actual result |
| --- | --- |
| Real launcher and native MCP build | Exit 0; no warnings; compiler JSON empty |
| Recursive callback and scalar positive probes | Both build and run with exit 0, stdout `false`, empty stderr |
| Public stdlib contract | 4 passed; exit 0 |
| Raw native launcher acceptance | 3 passed; exit 0 |
| Native MCP stdio acceptance | 4 passed; exit 0, including authorized real screenshot and restricted grants |
| Aggregate focused launcher suite | 11 executable checks plus 2 positive ownership probes; exit 0, all runtime stderr empty |
| Native channel regression | 19 passed in 7.122 seconds; exit 0 |
| Real API compile-fail checks with the updated compiler | E3014 private construction, E3047 task transfer, E4210 borrow escape, E4101 overlapping borrows; invalid binaries not executed |
| Complete `CARGO_BUILD_JOBS=2 cargo test` | Exit 0; 318 passed, 0 failed, 1 ignored, 78 result groups; no warnings |

The ignored Cargo test is the existing host-dependent `native_baseline`; no
filter or suppression was added. Focused tests are reproduced with
`bash scripts/test-launcher.sh`. Their compiler diagnostics, stdout, stderr and
exit records are in `.build/launcher-tests/`. Native protocol regression is in
`.build/evidence/phase4/protocol.log`, ownership diagnostics in
`.build/launcher-tests/ownership/`, and the full Cargo log in
`.build/evidence/cargo-test-migration.log`.

The launcher migration has no remaining demonstrated stdlib/compiler blocker.
The recursive-result false rejection and SIGTERM/SIGPIPE issues were fixed in
the supplied Tarn toolchain; no workaround was introduced here. The tests also
cover exact rejection of unknown JSON-RPC fields containing commas, malformed and
duplicate JSON, and extra bytes on protocol stdout. Readiness registrations are
retired at pipe EOF and receive progress resets the idle timeout to preserve the
Python socket behavior.

This closes the launcher migration and the listed acceptance checks. A runtime
project-build tool, saved screenshot artifact store and broader MCP transports
are outside this launcher change. The known GTK render/LSan findings remain
separate and unresolved; this report does not turn their earlier failures green.

## Phase 4 follow-up: private captures and fixed project build

Private capture persistence is pending a public stdlib capability. With the
currently supplied compiler, `fs.create_dir` and `File.create_new` expose no
explicit creation mode. A focused executable probe under umask 022 produced
0755 directories and 0644 files. Evidence is preserved under
`.build/evidence/phase4-completion/`. Capture persistence requires atomic
creation with directory mode 0700 and exclusive file mode 0600. No global umask
change, permission utility, extra FFI or compiler modification was introduced.
The user answered yes to incorporating this capability; its API and updated
toolchain have not yet been supplied or validated here.

Independent build work now has ordinary Tarn modules `devtools/build.tarn` and
`devtools/process_run.tarn`. They snapshot the fixed counter sources into a fresh
build directory and invoke pkg-config, cc and Tarn directly with argument vectors.
The recipe has a 60-second deadline, bounded pipe draining and explicit
terminate/kill/wait cleanup of each directly owned process. Signals do not promise
termination of an entire process tree. SDK flags containing quoting or escaping
are currently rejected explicitly. Build roots and compiler paths must be absolute.

`bash scripts/test-project-build.sh` passed two executable checks: the actual GTK
counter compiled with exit 0 and empty diagnostics; an isolated invalid project
returned BUILD_FAILED with exit 1 and preserved the compiler's real E2001 record,
including file, line and column. The test executable compiled without warnings.
These modules are not yet exposed as an MCP tool. No new complete Cargo run or
render/LSan success is claimed by these focused results. MCP integration, private
artifact persistence and the complete build-to-capture acceptance remain pending.
