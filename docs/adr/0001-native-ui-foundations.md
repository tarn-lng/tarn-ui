# ADR 0001: native UI foundations and scoped GTK4 feasibility

Status: **architectural direction approved**, with the constraints below.
Phase 5 component update (2026-10-22): keyed labels/buttons and retained native
pages now support owned property views and a bounded Tarn route history.
The native ABI is 11. A per-session registry borrows widgets from the native tree;
it is released with the scoped session. There is no global component registry or
Tarn callback crossing the FFI boundary. See
[components and navigation](../components-navigation.md).
Phase 5 update (2026-10-22): the user authorized reactive UI work and deferred
Mesa/EGL investigation. The first implementation uses owned state, numeric
reducers and owned counter view descriptions, preserving native widget identity
and scoped cleanup. No callbacks receive native authority. See
[reactivity](../reactivity.md) and [the current roadmap](../roadmap.md).
Earlier phase-only authorization paragraphs below are historical.
Phase 3 update (2026-10-21): the user authorized inspect/click/close development
control after accepting Phase 2. The optional build uses an inherited private
Unix socket capability, explicit operation/text grants and session binding.
Applied-event completion remains in Tarn; production omits the transport.
See [protocol guide](../devtools.md) and [measured results](../phase3-results.md).
Screenshot/MCP and later phases remain deferred.
Phase 2 update: the user accepted Phase 1 and explicitly authorized the counter
only, with targeted executable tests. Counter state and checked increment logic
are Tarn application code; native callbacks enqueue events. See
[the implementation guide](../counter.md) and [actual results](../phase2-results.md).
Later phases remain deferred. This authorization supersedes the earlier
Phase-1-only scope paragraph below; it does not claim a green full baseline or LSan.
Implementation update (2026-10-20): Phase 1 now exists and its native heading
window was observed by the user. Focused checks were explicitly authorized in
place of rerunning the expensive baseline. Per-session cleanup and Tarn paths
pass; process-wide LSan and the original full-baseline completion gates are not
claimed green. See [implementation and current evidence](../native-ui.md).
The earlier feasibility observations below are historical.
Feasibility gate blocked by baseline local HTTPS connectivity; no executable UI milestone passed.
Current setup and probe results: [development guide](../development.md). Earlier
missing-toolchain observations below are historical.

## Approved implementation constraints

Authorization covers the compiler feasibility gate and Phase 1 only: a native
window with a heading and deterministic scoped cleanup. Compile the scoped-session
and `mut fn` model against the real Tarn compiler before writing GTK integration.
Counter behavior, reactive state, MCP and mobile implementation remain deferred.

The portable API exposes semantic window/session/events and owned configuration,
not GtkWidget, GObject, GLib context or GTK reference concepts. Keep opaque backend
storage and backend entry selection private to the adapter; the Session sketch
below illustrates that private storage, not a public GTK handle contract.
Alternative Qt, SwiftUI and Compose adapters must remain possible.

Events are delivered serially in native dispatch order, without reentrant Tarn
application calls. Closing invalidates subsequent widget actions and supersedes
obsolete pending actions. All native operations and event dispatch run on the
designated UI thread. A future asynchronous design should run non-UI work outside
that thread, return owned result messages through a bounded queue and wake the
native loop to apply them on the UI thread. It must preserve per-producer ordering,
define cross-producer ordering at admission and reject late results after closure.
Never block GTK waiting for a worker; wake integration and shutdown ownership need
their own reviewed tests. No such async machinery is implemented in Phase 1.

Production builds must omit development/agent control listeners and endpoints by
default. Phase 1 introduces no control channel. Later development builds require
explicit opt-in and session authorization; importing the portable API grants none.
No compiler intrinsics, trusted UI module or borrow-checking exceptions are allowed.

Historical environment output is retained locally at `evidence/history/feasibility-environment.json` and excluded from Git.

Repository baseline: Tarn `2a2e39e8638db219c214bfecfb7de9febed0963c`.
See [evidence](../evidence.md) for source anchors and verification limitations.

## Decision

Use **GTK4**, initially without libadwaita, behind a small C bridge. Keep Tarn UI
independently versioned, outside compiler and standard library. Use ordinary
Tarn functions, structs, enums and local mutable callables. GTK owns layout,
rendering, input and accessibility. No browser, HTML, JavaScript, QML-generated
web content or framework-owned drawing engine is introduced.

The smallest candidate slice uses a **scoped session and event pull API**:
native signals invoke only C handlers; handlers queue value events; Tarn pulls
events and updates its own state. No Tarn closure is passed as a C function
pointer. A Tarn higher-order runner creates the session, calls a local Tarn
body and closes the native session before returning its result.

This is source-grounded feasibility, not proof that the proposed API compiles.
The first implementation gate must compile the scoped callable/borrow shape
with the unmodified compiler. Failure must produce a minimal reproducer before
any language change is considered.

No compiler/runtime change is presently essential for this restricted slice.
Automatic destruction of freely owned native values, direct foreign callbacks,
and non-Linux targets remain missing capabilities for a broader framework.

## Current Tarn capabilities

| Area | Implemented evidence | Constraint for UI |
| --- | --- | --- |
| Native compilation | Cranelift, post-drop IR, ELF/system cc, Linux x86_64 | No supported ARM64/mobile/Windows ABI assumed |
| Ownership | Non-Copy owners, moves/partial moves, NLL borrows, provenance, verified explicit drop plans | User-defined native destructors unavailable; pointer fields are not automatically freed |
| Structs/enums/interfaces | Generic ADTs, static dispatch, borrowed dynamic interfaces | Reference fields forbidden (E4203); owned dynamic interface objects unavailable |
| Closures | Shared/mutable/once call modes, owned captures, borrowed nonescaping environments, capture destruction | Private code/environment ABI, not a C callback ABI; moved references retain loans |
| Outbound C FFI | Scalar/pointer parameters and results, unsafe calls, CString and copying readers | No by-value structs, slices, strings, variadics, arbitrary function-pointer ABI or supported C exports |
| Foreign handles | Empty pointee structs and raw pointers | Raw pointers are Copy, carry no loans, neither Transfer nor Share; wrapper author must guarantee validity |
| Linking | Explicit validated `--link name` / `--link :exact-file` | No source grants, arbitrary linker paths/options, native package hooks or dynamic-loading API |
| Threads/async | Native tasks, Transfer/Share, scoped completion, mutexes/atomics, cooperative async/readiness | No GTK main-context executor integration; task scheduling does not confer UI-thread access |
| Tooling | JSON diagnostics and command errors, test runner, LSP, verified source package graph | No MCP server or semantic UI protocol found in complete tree |

Documentation contains historical/provisional wording. In particular, ordinary
Tarn closures are implemented even though parts of the language guide still
say provisional. Their implementation must not be mistaken for foreign callback
support. Backend C declarations are imports; ordinary functions are local symbols.
Existing internal native task adapters are specialized compiler machinery, not
a general user callback facility.

## Linux backend comparison

| Criterion | GTK4 / optional libadwaita | Qt 6 / Qt Quick / Kirigami |
| --- | --- | --- |
| Foreign boundary | C functions, GObject references/signals; scalar bridge straightforward | C++ QObject/metaobject/signal APIs require C++ bridge; QML engine adds object lifetime boundary |
| Linux integration | Strong GNOME integration, Wayland/X11, accessibility, themes; libadwaita supplies GNOME application patterns | Strong KDE integration; Kirigami supplies adaptive KDE patterns; Qt Quick controls have toolkit/platform styling |
| Native interpretation | Actual GTK controls and GTK layout/rendering | Qt is native compiled toolkit code, but Quick controls are scene-graph rendered and not host GTK widgets; discuss expectations before selecting |
| Event model | GLib context iteration plus native signal handlers can feed an event queue | Qt event loop/signals can feed same conceptual queue; must handle C++/QObject thread affinity and exceptions |
| First slice cost | Small C adapter; no introspection generator required | C++ ABI isolation, potentially moc and QML packaging; Qt Widgets is a simpler Qt alternative but not the requested Quick/Kirigami stack |
| Visual direction | GNOME conventions; libadwaita optional after first proof | KDE conventions and adaptive components; does not automatically imply host-native controls elsewhere |
| Distribution | GTK runtime plus C development headers for bridge source builds | Qt modules/plugins, optional QML modules, Kirigami dependencies |
| Licensing | Review actual GTK/libadwaita releases and redistribution obligations | Review actual Qt module/Kirigami licenses, dynamic/static distribution and commercial options |

Select GTK4 for the smaller ABI surface and first Linux experiment. Do not force
libadwaita's application styling into the portable core. Add it as an optional
GNOME extension once theme/native integration tests justify it. The comparison
is not an installed-toolkit benchmark; neither toolkit's SDK is available here.

Revisit Qt if KDE integration becomes primary, GTK's interaction/snapshot APIs
cannot satisfy acceptance, or measured bridge complexity favors Qt. Implementing
both now would dilute the ownership experiment. Native does not mean identical
appearance across desktops or platforms.

## Package and build boundary

Proposed repository layout (only documentation exists now):

```text
tarn-ui/
  core/                 ordinary Tarn value types and event model
  backends/linux/gtk/   Tarn wrapper + audited C bridge and header
  devtools/             transport, client API, build supervisor, later CLI
  mcp/                  later adapter over devtools API
  examples/counter/     Tarn application state and event handling
  tests/                native, integration, compile-fail, acceptance
  docs/                 ADRs, ABI safety contract, protocol, evidence
```

Package identities use `tarn_ui` / `tarn_ui_gtk`, because hyphens are invalid in
Tarn package names. The monorepo has its own version; protocol and bridge ABI
have explicit versions independent of the compiler. Start with local source
modules in the example; publish ordinary Tarn packages only after the package
boundaries work. No assumed Cargo-like Tarn workspace feature or path dependency
schema is added to `tarn.toml`.

The verified manager accepts a pure-source inventory, excludes C/shared-library
artifacts and rejects unknown manifest fields, build hooks and permissions.
Therefore `tarn add tarn_ui` cannot currently build/install the native bridge.
Do not disguise C as source data or add a trusted compiler module.

For the first developer build, use a separately reviewed project build recipe:
compile a position-independent bridge shared library against GTK using the
installed SDK's pkg-config metadata, put it in an explicit application-owned
build directory, then invoke existing `tarn build --link tarn_ui_gtk` with a
command-scoped absolute `LIBRARY_PATH` selecting that directory. Launch the
binary with a command-scoped absolute runtime library search directory, avoiding
empty/current-directory entries. This uses the system cc/loader behavior; Tarn
does not implement native artifact discovery or shared library production.
The bridge's GTK dependencies are linked by its explicit C build recipe.
Record bridge source hash, ABI, toolchain, pkg-config versions and library paths
in build evidence; reject an ABI mismatch before creating widgets.

This recipe is a proposal to test, not a working command shipped now. A static
bridge plus explicit GTK/GObject/GLib/GIO link grants is an alternative if shared
loader behavior proves awkward. Never pass arbitrary pkg-config text through
Tarn's validated `--link`, or execute a recipe merely because a package import
requested it. No privileged install is part of ordinary app execution. Offer a
documented user-space SDK/container or suitable developer image; source builds
still require an available SDK. Bundled artifacts need a later integrity,
provenance, license and native authority design, outside Phase 1.

## Ownership and lifecycle

### Scoped ownership instead of pretend RAII

Only the runner owns the native session. Public code receives `&mut Session`;
its native pointer field and constructor/destructor are private. Session is an
ordinary non-Copy struct containing a pointer, so it is neither Transfer nor
Share through existing structural checks. Never represent its authority as just
a transferable integer. The callback returns only `Result<void, Error>`, with
owned error fields and no session-derived reference; no generic escape channel
is needed in the first API.

The runner calls the Tarn body, stores its result, closes the session, then
returns the result. It must not use `try` between successful creation and cleanup
in a way that bypasses close. All partial native creation failures clean up
inside C before returning failure. Return, `try` inside the body, and ordinary
loop exits all return control to the runner. Panic is whole-process abort in
Tarn: no unwind/cleanup guarantee is claimed for panic, forced termination or
calling `os.exit` inside the body. Normal window close must clean up explicitly.
Use sanitizers/weak finalization evidence, not process exit, to establish cleanup.

No independently owning Window/Widget values are exposed in Phases 1–3.
Operations on stable IDs always borrow the session and fail after native window
closure. Future ordinary non-Copy owners require a new resource-destruction
design before being advertised as automatically safe to drop. Stored reference
fields must not be introduced to simulate parent lifetimes.

### GTK object graph

C allocates a stable heap session before signal registration. Its address never
points into Tarn stack/closure storage. The bridge owns the top-level window
according to GTK reference rules; GTK parent-child ownership controls the box,
heading, label and button. Write down each transfer/floating-reference/ref/unref
operation against the chosen GTK version; do not unref a child both as parent
property and as independent owner. Additional observer references, if required,
are explicit and balanced. Stable-ID lookup is semantic mapping, not an alternate
owner-liveness system replacing Tarn's post-drop rules.

Closing moves native session state Open → Closing → Closed. A close request
stops accepting actions and queues one Closed event. GTK may destroy widgets
while processing close; clear weak observations and prohibit subsequent queries
through stale widget pointers. Native session storage remains until the runner
releases it. On teardown: disable request dispatch, disconnect signal handlers
and destroy GLib sources, discard queued values, destroy/unref the window and
remaining owned references, release protocol buffers/endpoints, then free the
session. Handle partially initialized sessions with one cleanup path. Do not
drain arbitrary user callbacks while tearing down.

C signal userdata contains only session pointer plus immutable event/element
identity. Disconnect before freeing it. It owns no Tarn closure and no strong
reference back to its widget; this avoids a widget → callback → widget cycle.
Queue events as copied scalars/owned byte data, never borrowed event pointers.
Bound the queue; overflow becomes a terminal explicit error and still cleans up.
Closed takes priority over obsolete pending clicks.

### Thread and event-loop contract

Create GTK and run every widget operation on the process's designated UI/main
thread. Pointer-containing Session prevents safe task transfer but cannot prove
that initial creation occurred on the main thread. The first example invokes
the runner directly from main, forbids a second/reentrant session, and documents
this precondition on the low-level runner. Until a reliable platform thread
check exists, mark that entry unsafe rather than claiming compile-time main
thread enforcement. Add runtime creating-thread checks on bridge operations as
defense in depth. Do not capture Session in `spawn`, even behind a Mutex.

`next_event` performs GLib context iterations on that thread until an event or
terminal error occurs. C callbacks enqueue only; control returns to Tarn before
application logic runs. Avoid busy polling. This is native event-loop integration,
not a claim that Tarn's epoll executor already drives GTK. A protocol descriptor
can later attach a bounded GLib source; requests execute on the same UI thread.
No detached UI thread, hidden global executor or cross-thread widget call.
Async application work and main-thread messaging are a separate future design.

## Bridge ABI and safety contract

Use versioned scalar/pointer C functions only, conceptually:

```c
uint32_t tarn_ui_gtk_abi_version(void);
int32_t tarn_ui_gtk_create(void **out_session);
int32_t tarn_ui_gtk_next_event(void *session); /* positive event / negative error */
int32_t tarn_ui_gtk_set_counter(void *session, int64_t value);
int32_t tarn_ui_gtk_close(void *session);
```

These are proposed interfaces, not implemented declarations. Exact close result
and ownership transfer must be specified before coding. Only the private runner
calls close, exactly once. Failure of close still consumes native session storage;
normal native errors return status rather than longjmp, exceptions or abort.
Event values distinguish Increment, Closed and terminal native failure. Setting
the label formats the Tarn-supplied number; C never increments application state.

Out pointers are valid only during synchronous calls. C must not retain
`ffi.of_mut` addresses, Tarn strings, vectors, borrowed slices or closure
environments. All input text is validated/copied synchronously; CString lives
through the call. Native returned text is copied to owned Tarn data before
mutation or destruction. No C pointer becomes a safe Tarn reference.
No private Tarn aggregate layout crosses the ABI. Counts and statuses are range
checked; no unchecked enum casts. C catches no Tarn panic because C never calls
Tarn. Any future C++ backend catches all C++ exceptions before its C ABI.

GTK's own allocation/fatal-error behavior remains a native dependency risk.
Safe wrapper claims cover valid documented native API behavior, not arbitrary
corruption or bugs in C. `unsafe` never bypasses move or borrow checks.

## Minimal Tarn API proposal

Signatures below are design sketches, not compiled examples:

```tarn
pub enum Event { Increment Closed }
pub struct Session { native *mut NativeSession }
pub unsafe fn run(body mut fn(&mut Session) Result<void, Error>) Result<void, Error>
pub fn Session.next_event(&mut self) Result<Event, Error>
pub fn Session.set_counter(&mut self, value i64) Result<void, Error>
```

`NativeSession` and the native field are private implementation details. The
runner's unsafe precondition is designated main-thread creation, not permission
to violate lifetime rules. Public method bodies contain the small audited unsafe
boundary; ordinary counter state manipulation stays safe Tarn.

The example declares `var counter: i64 = 0` and passes a local `mut fn` body to
run. It displays zero, loops on next_event, assigns `counter = counter + 1` for
Increment, then calls set_counter. Closed breaks the loop and returns Ok.
At maximum i64, return an owned overflow error through the runner instead of
triggering Tarn's checked-arithmetic abort. The app owns the numeric truth; C
owns GTK objects, event queue and presentation. Describe the result as
**Tarn-driven application logic over a C/GTK bridge**, never fully Tarn.

Declarative construction, reactive state cells and component closure storage
are deferred. Later components can be ordinary functions producing owned value
descriptions with stable keys and platform capabilities. Reconciliation should
retain native controls; subscriptions need explicit disposal and owned events.
Do not introduce a DSL, GC, shared-mutable state bypass, global widget registry
or framework layout renderer to anticipate Phase 5.

## Provider-independent agent protocol

Version 1 proposal: bounded UTF-8 JSON Lines request/response records over an
explicitly inherited local development channel. Default production builds expose
no channel. Launcher-issued development authorization must bind the session,
allowed operations and data/screenshot access. A socket path or process ID alone
is not authorization. A future local socket requires private directory/socket
permissions, peer checks and a per-session credential not logged or passed in
world-readable command arguments. Never listen on public TCP by default.

Every request has `version`, `request_id`, `method`, `params`; every response
echoes version/request_id and has either result or `{code,message}` error.
Reject unsupported versions, unknown fields/methods, invalid UTF-8, oversized
records (initial limit 1 MiB), invalid IDs and unauthorized requests. Serialize
actions in request order with a monotonic UI revision. No hardcoded sleeps for
settlement: inspect after an action revision has been applied by Tarn.

Implement only inspect/click/close initially in Phase 3; screenshot/diagnostics
in Phase 4. Close is needed for acceptance teardown even though it was not in
the initial suggested list. `ui.type` is unnecessary for a counter and remains
unimplemented. `ui.rebuild` belongs to a separately authorized host build
supervisor using fixed argv, not arbitrary app-side shell execution; no command
or method is advertised before its implementation and tests exist.

Conceptual request:

```json
{"version":1,"request_id":"7","method":"ui.click","params":{"window_id":"main","element_id":"increment"}}
```

Inspection snapshots include:

| Field | Required meaning |
| --- | --- |
| session_id, revision | Launch identity and consistent UI revision |
| platform, backend, backend_version | Actual Linux/GTK and toolkit version |
| window_id | Stable logical ID `main`, not an invented compositor ID |
| id, type, native_type | Stable app ID, semantic role, actual GTK class |
| text, accessibility_label | Native getter/accessibility data, null if unavailable |
| enabled, visible | Current native sensitivity/visibility, including ancestor effects |
| parent_id, children | Current native widget relationships, deterministic child order |
| bounds | Actual measured coordinates with units/space; null plus reason when unavailable |

Register fixed IDs `main`, `heading`, `counter`, `increment` on actual native
widgets. Include internal GTK children using deterministic scoped paths where
possible, marking their IDs unstable if toolkit implementation details change.
Do not substitute a desired component tree for the real hierarchy. Traverse GTK
objects on the UI thread; read label text and sensitivity through GTK getters.
The ID table locates widgets, never supplies their text/value. Include real
containers; stable application IDs survive re-inspection, not necessarily app
redesign. Duplicate application IDs fail creation.

GTK window-local bounds can be measured after allocation. Wayland may deny
global screen coordinates: report null global bounds and real window-local
bounds, with scale and coordinate space. Never fabricate desktop positions.
Inspection of a hidden or not-yet-allocated widget reports unavailable bounds.
Accessible labels and widget text can contain sensitive data: authorization must
explicitly allow exposure, redact protected controls by policy and mark redaction.
Do not log snapshots or screenshots by default.

`ui.click` resolves the ID in the current window and invokes the native button
activation path, not the Tarn increment function. Reject missing/wrong-type,
disabled, hidden or closed targets. A click acknowledgment represents an applied
Tarn event/update revision, not merely queuing a C signal. One pending action can
carry a scalar request ID through the native event queue; the wrapper signals
completion after the Tarn handler updates the widget, before processing the next
request. Detect absent completion as a timeout, never manufacture success.
Read-only inspect observes the corresponding native label afterward.

Screenshot captures actual rendered window content via supported GTK snapshot/
paintable/GSK/GDK facilities after allocation and frame completion; validate
realization/renderer requirements on the selected GTK release. Return an artifact
hash, dimensions, format and revision. Window-content capture is sufficient for
the first acceptance; it does not promise desktop decorations or full-screen
capture. Compositor/portal desktop capture is a separate permissioned capability.
Failure or unsupported renderer returns an explicit error, not a generated image.

Proposed stable errors: `UNAUTHORIZED`, `UNSUPPORTED_VERSION`, `INVALID_REQUEST`,
`UNSUPPORTED_OPERATION`, `NOT_FOUND`, `WRONG_ELEMENT_TYPE`, `DISABLED`, `HIDDEN`,
`WINDOW_CLOSED`, `WRONG_THREAD`, `NOT_READY`, `QUEUE_FULL`, `TIMEOUT`,
`ABI_MISMATCH`, `NATIVE_ERROR`, `BUILD_FAILED`, `CAPTURE_UNAVAILABLE`.
Diagnostics carry stage/code/message and owned details; preserve Tarn diagnostic
records rather than parsing human output. `tarn run --json` also streams app
stdout, so do not treat it as an exclusively structured protocol channel.

CLI and MCP reuse one typed client/transport implementation. MCP translates tool
calls/results only; it owns no widget lookup, authorization bypass, state store
or separate interaction implementation. No LLM vendor enters protocol semantics.

## Missing features and narrowly scoped future improvements

| Missing capability | Need now? | Smallest defensible future work |
| --- | --- | --- |
| Foreign C callbacks/exports | No, pull events avoid them | Separate C-callable function-pointer/export design with scoped userdata and deregistration, callback ABI tests, reentrancy/thread/panic rules; never cast private closure pairs |
| User-package native resource destruction | No for scoped runner; yes for independently owned windows | General-purpose verified foreign resource contracts tied to resolved package/declaration identity, consuming close and post-drop glue; design Copy/partial move/capability rules first |
| Main-thread authority | Entry precondition now | Safe bootstrap capability or verified platform thread admission, no trusted UI module name |
| Native artifact/package integration | Explicit developer recipe now | Application-authorized native artifacts/link inputs with hashes/target/toolchain and enforced execution policy; no dependency self-grants |
| GTK/Tarn async integration | No | Main-thread command queue plus explicit wake integration and shutdown semantics, tested independently |
| ARM64, platform C ABIs | No | General target selection, layouts/calling conventions/object/link tests before mobile wrappers |

These are proposals, not authorization to add compiler features. Scope a separate
ADR and executable failing example for each actual limitation. Existing trusted
resource catalogs illustrate required rigor, but adding `ui` to TRUSTED_STDLIB
would violate the independent-package goal. Do not relax stored-reference or
borrow rules, or introduce destructors globally, to ship the counter.

## Milestones and executable gates

Phase 0 deliverable is this proposal and the evidence record. Architecture is
ready for review, but compiler/toolkit feasibility has not been executed here.

| Phase | Deliverable | Required evidence before completion |
| --- | --- | --- |
| 1 | Scoped GTK session, native heading window/event loop, teardown, explicit build recipe | Full Tarn baseline green; scoped API compiles unchanged; window observed under Linux display; close and partial-create failure release all bridge/object refs; no render/web runtime dependency |
| 2 | Tarn counter state, native button, C-only signal handlers, UI updates | Real and semantic activation change label exactly once; multiple events work; early return/error cleanup; overflow returns error; compile-fail borrowed session escape, private handle construction and task transfer |
| 3 | Authorized inspect/click/close protocol | Native hierarchy/ID/text/disabled-state checks; no state shadow; ordered action acknowledgment; malformed/unauthorized/version/missing ID/stale window/queue overflow tests; counter acceptance through state assertion |
| 4 | Real screenshots, shared CLI/MCP client, diagnostics | Full acceptance below; PNG valid/nonempty at acknowledged revision; MCP/CLI protocol equivalence; captured data access authorization and denial tests; explicit build failure results |
| 5 | Declarative/reactive/native layout/navigation/theme/accessibility | Begin only after 1–4 pass; component disposal, stable identity and native accessibility tests, no custom renderer |
| 6 | Mobile readiness then separately reviewed implementations | Target/ABI evidence and platform lifecycle proof precede native mobile backends |

Run unit tests for event/status encoding, queue bounds, stable IDs, protocol
validation, revision ordering and lifecycle transitions. Native C bridge tests
exercise callback deregistration, partial initialization, duplicate IDs,
creating-thread violations and repeated create/close under ASan/UBSan and leak
checks where supported. GTK may keep process-global caches; distinguish them from
live per-session references using weak-finalization observations and allocation
accounting in tests. Do not suppress all leaks to obtain a pass.

Negative Tarn tests must invoke the real compiler and assert the appropriate
existing diagnostic (E3014 private fields, E3047 transfer, E4203 stored references,
borrow/escape codes as emitted); do not guess all escape codes in advance. Include
body-return errors, close after click, close during pending protocol action,
disabled button, missing label, queue overflow and connection loss. C callbacks
must not run after session free; establish with sanitizer stress and handler
disconnect accounting. No compiler changes means no invented compiler unit tests.

### Full agent acceptance (Phases 3–4)

Use pinned source/toolchain/bridge versions and a documented GTK-capable CI image
with an isolated compositor or Xvfb session. Report toolkit/display versions.
Use monotonic deadlines and readiness notifications; missing SDK/display is an
explicit environment failure in required CI, never a passing skip.

1. Build bridge and actual Tarn counter, preserving compiler JSON diagnostics.
2. Launch with explicit development authorization; wait for window allocation
   and protocol readiness. Record session identity.
3. Inspect the actual native hierarchy; require `main` and `increment`, the
   latter a visible enabled native button. Missing button fails `NOT_FOUND`.
4. Read and strictly parse `counter` widget's native label as initial value N.
5. Click `increment` semantically and wait for the applied revision acknowledgment.
6. Inspect at/after that revision; require the same window/session and value N+1.
   Unchanged/different value fails `STATE_MISMATCH`; no C-side expected counter.
7. Capture the actual window at/after that revision; require a valid nonempty PNG
   artifact with dimensions/hash. Screenshot failure fails full acceptance.
8. Send native close, await process completion within deadline, require zero exit
   and per-session teardown evidence. Check handler/source/ref/allocation counts
   and sanitizer result. A timeout fails; forced cleanup by the test is recorded
   and never counts as clean application shutdown.

Emit one deterministic structured record per step, final success/failure and
stable test error codes (`BUILD_FAILED`, `LAUNCH_FAILED`, `NOT_FOUND`,
`STATE_MISMATCH`, `CAPTURE_UNAVAILABLE`, `SHUTDOWN_FAILED`, `TIMEOUT`). Sort keys
and hierarchy consistently; times, session IDs and image bytes are real variable
data rather than promised bit-identical results. Test exits nonzero on any failed
required step. Harness always attempts bounded teardown in a finally path.

## Platform strategy (documentation only)

Share semantic intent, owned event values and capability discovery, not pixel
layouts or a forced universal visual vocabulary. Backend-specific components
live in optional packages; unsupported features return explicit capabilities.
Do not expose raw native pointers in portable APIs.

- **iOS:** eventually SwiftUI views/state adapters hosted by Swift/UIKit app
  lifecycle and main actor. A C-compatible bridge can carry owned events/commands;
  navigation, controls, accessibility, system animations and Liquid Glass remain
  native, subject to OS availability. UIKit is the escape hatch for required
  controls/lifecycle hosting. xlinux may assist Linux orchestration/build tasks
  where compatible, but it does not establish Tarn ARM64/Darwin ABI support or
  replace Apple SDK, signing, device/simulator and distribution requirements.
- **Android:** Jetpack Compose/Material with native Activity lifecycle, managed
  runtime/JNI boundary and main Looper. Later commands/events must cope with
  recreation, backgrounding and saved state. No Tarn function-pointer assumptions
  across JNI; ART object references require deliberate ownership/cleanup.
- **Windows:** WinUI 3 with a C++/WinRT bridge, COM reference lifetimes, dispatcher
  queue and native app packaging. General Windows target/ABI support comes first.

Swift ARC, ART/JVM runtime and COM are platform integration mechanisms, not a new
garbage collector for Tarn. Their foreign object lifetimes must remain explicit
at the boundary. None of these backends is implemented by this milestone.

## Risks, alternatives and open review questions

The scoped runner limits API expressiveness but avoids making unsupported drop
claims. A direct `open`/`close` API is possible today but leaks on forgotten/error
paths; it is inferior for the first public safety contract. A separate native
host process with owned IPC would contain some C failures and remove in-process
FFI callbacks, but adds lifecycle/transport complexity; reserve it if linking or
scoped ownership fails. Broad compiler callback/destructor work is premature.

Reentrant GTK signals, native close invalidation, actual activation semantics,
frame-ready screenshot capture, toolkit caches and Wayland bounds need real GTK
tests. Main-thread admission is unresolved beyond the explicit entry contract.
Package-native artifact verification and distribution are not solved by the
source lock. GTK is GNOME-oriented and may feel less native on KDE; Qt remains
the principal alternative. Source-level evidence cannot certify an uncompiled
safe wrapper; no feasibility or safety milestone should be marked passed yet.

Review must settle the scoped API/unsafe main-thread entry, GTK4-first choice,
explicit native build boundary and staged protocol scope. Exact GTK reference
ownership and screenshot APIs become executable implementation evidence in the
corresponding phases, with this ADR updated if experiments contradict it.

## Exact next step after architecture review

Obtain a complete pinned checkout in an unprivileged developer environment with
Rust/Cargo, system cc and GTK4 SDK/display support, and run Tarn's baseline tests.
Then compile a tiny non-UI scoped-session/`mut fn` probe using only existing
Tarn FFI to verify privacy, borrow containment, error-return cleanup and rejected
task transfer. If it passes, implement **Phase 1 only**: one C-owned GTK heading
window driven and closed through that scoped Tarn runner, with teardown evidence.
If it fails, preserve the minimal diagnostic/reproducer and propose the smallest
general-purpose fix before proceeding. Do not start with callbacks, reactive
state, a CLI, MCP, mobile backends or a compiler trust exception.

Current containment evidence: [failed negative safety probe](../scoped-session-blocker.md).
The proposed scoped API is not verified safe by the pinned compiler.

Correction verification update: the user-provided uncommitted compiler correction
rejects the escape probes and passes the borrow suites. The original clean pin
still has the defect; the patched source hashes and results are recorded in
`evidence/fix-verification/`. The complete baseline remains blocked by localhost
HTTPS connectivity, so no native UI milestone has passed.

Current verification update: clean da3173c (including bde1757) is now pinned in
the setup script. Both safety probe scripts and the positive scoped session pass.
An independent local TCP bind fails with EPERM in this executor, preventing the
full baseline HTTPS fixture from running. See `evidence/da3173c/README.md`.
