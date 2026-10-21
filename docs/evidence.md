# Phase 0 evidence record

Audited repository: https://github.com/tarn-lng/tarn

Pinned revision: `2a2e39e8638db219c214bfecfb7de9febed0963c` (main at inspection).
Audit date: 2026-10-21.

## Method and limits

The initial workspace contained no checkout. `git clone` failed because the
configured proxy at port 8080 could not be reached. Read-only GitHub connector
requests retrieved the complete repository tree (not truncated) and selected
UTF-8 files at the pinned revision. Those files are saved under `/workspace/tarn`
as a **partial reference snapshot**, not a Git checkout or buildable distribution.
The new deliverable is `/workspace/tarn-ui`; upstream files were not edited.
No commits, remote writes, dependency installation or compiler changes occurred.

Read the repository's root AGENTS.md, architecture, language and roadmap; actual
FFI ADR is `docs/adr/0047-native-c-ffi.md`. The suggested
`0047-minimal-native-c-ffi.md` does not exist. Compared current source against
the ADR, FFI guide, ownership guide, closure ADR, package guide and Phase 18 report.
Historical context in an accepted ADR is not treated as current implementation.

## Source anchors

All links below resolve at the audited revision. Line numbers may change later.

| Finding | Implementation evidence |
| --- | --- |
| Scalar/pointer outbound C ABI, aggregate rejection | [codegen.rs L169](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/compiler/backend/src/codegen.rs#L169), `c_type`, `c_signature_types` |
| C declarations imported; normal Tarn functions local, no public C export path | [codegen.rs L349](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/compiler/backend/src/codegen.rs#L349); special exported main is not a callback API |
| Explicit library grants, no library paths/options | [backend lib.rs L70](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/compiler/backend/src/lib.rs#L70), `valid_library`, `build_entry` |
| Pointer operations do not maintain loans | [ffi.rs](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/compiler/backend/src/ffi.rs), [ffi.tarn](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/stdlib/ffi/ffi.tarn) |
| Pointers neither Transfer nor Share | [capabilities.rs L104](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/compiler/types/src/capabilities.rs#L104) |
| Private fields checked on access and construction | [check.rs](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/compiler/types/src/check.rs), `field`, `struct_lit`, E3014 |
| Trusted sources are explicitly embedded; packages do not gain intrinsic authority | [driver lib.rs](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/compiler/driver/src/lib.rs), `TRUSTED_STDLIB`, `PackageSet`, `load_editor_sources` |
| Package schema rejects hooks/native metadata; lowercase identifier names | [manifest.rs](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/tools/packages/src/manifest.rs), `package_name`, `keys`, `Manifest::read` |
| Publication inventory excludes native C/library artifacts | [store.rs](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/tools/packages/src/store.rs), inventory validation and collection |
| JSON diagnostics/native failures; run stdout is application output | [CLI main.rs](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/tools/cli/src/main.rs), `native`, `native_error`; [JSON tests](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/tools/cli/tests/json.rs) |
| Explicit close wrappers already leak when omitted | [SQLite example](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/examples/sqlite/sqlite.tarn), [Phase 18 report](https://github.com/tarn-lng/tarn/blob/2a2e39e8638db219c214bfecfb7de9febed0963c/docs/phase-18-report.md) |

Additional reviewed contracts: ADRs 0026, 0032, 0033, 0047, 0051;
`docs/architecture.md`, `language.md`, `ownership.md`, `packages.md`, `ffi.md`,
`cli.md`, `roadmap.md`; `tools/cli/src/args.rs` and native integration tests.
The complete tree has no MCP implementation or UI backend. Existing LSP and
JSON output are useful foundations, not an implemented UI agent protocol.

## Executable evidence

| Check | Observed result |
| --- | --- |
| `cargo test` | exit 127: `cargo: command not found`; baseline not verified |
| `command -v cc` | `/usr/bin/cc` |
| `command -v pkg-config` | `/usr/bin/pkg-config` |
| `pkg-config --modversion gtk4 libadwaita-1 Qt6Core Qt6Quick` | exit 1: all requested development packages unavailable |
| Compiler probes / native UI tests | not run: incomplete snapshot and missing toolchain/toolkits |

Upstream contains FFI scalar/pointer native fixtures, closure pass/fail suites,
CLI linking/SQLite tests, and ownership/drop contract suites. Their existence is
source evidence; historical passing counts in reports are not a test result from
this session. No implementation milestone is marked complete.

The toolkit comparison is a design assessment, not a measured benchmark or
completed integration experiment. Phase 1 must validate the toolkit assumptions
against the selected installed GTK version and desktop session. Relevant primary
API references for that validation:

- https://docs.gtk.org/gtk4/ (window/widget ownership, activation, snapshot APIs)
- https://docs.gtk.org/glib/method.MainContext.iteration.html
- https://docs.gtk.org/gobject/ (references and signal lifetime rules)
- https://gnome.pages.gitlab.gnome.org/libadwaita/doc/main/
- https://doc.qt.io/qt-6/qtquick-index.html
- https://develop.kde.org/frameworks/kirigami/

These external reference links are supplied for implementation review; they were
not fetched or experimentally validated during this repository audit.
