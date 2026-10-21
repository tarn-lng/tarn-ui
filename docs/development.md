# Reproducible development environment

Current development commands and Phase 2 results are in [the counter guide](counter.md).
The setup and execution observations below are historical; raw outputs are local
and excluded from Git. Do not repeat the full suite as a routine UI check.

Pinned Tarn: `da3173c4f012ead629aa03e67d3d9147615fec62`.
Latest verification: [clean checkout, probe results and TCP restriction](../evidence/da3173c/README.md).
The clean-checkout script now accepts this exact committed revision. Both negative
probe scripts and the positive session probe pass. Historical execution details
below describe the previous pin unless explicitly updated.
Upstream: https://github.com/tarn-lng/tarn . Supported target: Linux x86_64.
No Tarn source, compiler, safety rule or Cargo.lock has been changed.

## Current execution (2026-10-20)

This executor is CachyOS (Arch family), not Kubuntu. Rust/Cargo 1.99.1,
GCC 16.2.1, pkgconf 3.0.10 and system GTK4 4.24.1 are already available.
No installation or privileged command was needed. A C program including
`gtk/gtk.h` compiled, linked and ran against the system GTK4 runtime.
This checks development dependencies without opening a display.

Shell network access is restricted (`CODEX_SANDBOX_NETWORK_DISABLED=1`).
`git ls-remote https://github.com/tarn-lng/tarn.git HEAD` exited 128:
`Could not resolve host: github.com`. Network escalation is unavailable under
this session's approval policy. There is no need to install Rust/GTK here;
downloading additional dependencies through this shell is unavailable.

A complete local repository existed at `../tarn`, clean at the exact pin.
It was copied with `git clone --no-hardlinks ../tarn vendor/tarn`, then detached
at the pin. Git object integrity, clean worktree, non-shallow checkout, no sparse
checkout and no submodules were checked. This is a Git checkout, not the old
selected-file snapshot. The local origin is provenance, not independent remote
signature verification. The pin was taken from the existing evidence record.
The upstream source is ignored by the UI workspace; reproduce it with the script.

## Dependencies

- Git, bash and coreutils for checkout/setup.
- Rust and Cargo **1.99.1**, the exact tested version; edition 2024 support is
  required. Use official rustup distributions, or a sufficiently recent system
  Rust toolchain. No downloaded installation script is executed.
- System C compiler/linker and libc/libm headers (`base-devel` on CachyOS).
- pkg-config and GTK4 development/runtime packages (`gtk4`).
- curl, CA certificates, Python 3 and OpenSSL executable for Tarn's package/TLS
  tests. Tests need working localhost networking, including HTTPS with a local
  test CA. Never disable TLS verification to pass them.
- SQLite development/runtime package (`sqlite`) for SQLite integration.
  Upstream tests conditionally check a Debian-specific library location;
  a successful test process alone does not prove every optional branch ran.
- For the later window gate: a working Linux desktop display, or Xvfb + xauth
  and D-Bus (`xorg-server-xvfb`, `xorg-xauth`, `dbus`). No display test ran in this work.
- Exact Rust crates/checksums: unchanged `vendor/tarn/Cargo.lock` at the pin;
  the expanded graph is saved in `../evidence/cargo-tree.txt`.

System package versions depend on CachyOS rolling repositories; this recipe pins
Tarn, Cargo's graph and Rust, not an entire OS image. Record installed versions
with `dpkg-query -W` and `pkg-config --modversion gtk4` for comparison. The current execution targets CachyOS.

## Setup on CachyOS

The redundant `vendor/` copy has been removed. Scripts now use the existing
`../tarn` checkout without modifying it, and the scoped probe uses the installed
`tarn` from PATH (`/home/safases/.local/bin/tarn`, version 0.0.4).
The binary's version string does not prove its source revision. Earlier paths
and build results in this document and evidence logs describe the original run.

```bash
bash scripts/setup-cachyos.sh
# Or select another existing checkout:
TARN_SOURCE=/absolute/path/to/tarn bash scripts/setup-cachyos.sh
```

No installation is performed: setup checks installed tools and the offline Cargo
cache. If crates are missing, fetch them in a network-capable environment:

```bash
cargo fetch --locked --manifest-path ../tarn/Cargo.toml
```

If system dependencies are missing, install the official CachyOS/Arch packages
separately (administrator command, not executed here):

```bash
sudo pacman -S --needed base-devel git rustup gtk4 pkgconf sqlite curl ca-certificates python openssl xorg-server-xvfb xorg-xauth dbus
```

Use official rustup distributions if a toolchain is needed:
`rustup toolchain install 1.99.1 --profile minimal`. No downloaded installation
script is executed. System package versions follow the rolling repositories;
record them with `pacman -Q`. Build output remains in `.build/`.

## Verification and gates

```bash
mkdir -p evidence
RUSTUP_TOOLCHAIN=1.99.1 bash scripts/baseline.sh > evidence/baseline.log 2>&1
# Inspect exit status; a nonzero status fails the gate.
RUSTUP_TOOLCHAIN=1.99.1 bash scripts/scoped-probe.sh > evidence/scoped-probe.log 2>&1
```

The baseline runs `cargo test --locked --offline` with no test exclusions, ignored
failures or weakened verification. If needed, run `cargo fetch --locked` first;
the current run used the existing cache successfully. Offline Cargo does not
prevent tests themselves from using localhost networking.

**Observed baseline: exit 101.** Before Cargo stopped, summaries reported 21 passed
and one failed. Failure:
`https_consumption_validates_tls_and_locked_bytes` in `tools/cli/tests/packages.rs:374`,
`HTTPS registry download failed (curl status Some(7)); TLS validation and redirects
remain enforced`. That test starts a Python TLS server on 127.0.0.1 and contacts
`https://localhost:<port>/`. This is a local HTTPS connectivity failure, separate
from the failed external GitHub DNS check; the precise underlying network cause
has not been established. Cargo stopped early, so remaining suites were not run.
There were no emitted Rust warnings in the saved baseline output.

**Scoped probe: build and execution exit 0.** The original run built the pinned compiler and verified exact output.
`scoped-probe.sh` now uses the installed binary. The separate module keeps the raw
pointer private, accepts `mut fn(&mut Session) Result<void, Error>`, captures and
updates local state, and frees its malloc-backed handle before returning the body
result. The error path uses `try` inside the body. Output:

```text
true
closed
ok
closed
7
1
```

This is a non-UI feasibility probe, not a finished safe GTK API. No allocation
sanitizer, escape/private-construction/task-transfer negative tests, GTK resource
lifetime tests or native window test ran. `closed` is printed after free; it is
not independent leak instrumentation. Process abort/exit is outside scoped cleanup.

Phase 1 remains blocked by the failed baseline and incomplete negative safety
probes. After the full baseline succeeds in a network-capable local environment,
complete those probes against the real compiler before implementing a GTK bridge.
Only then build the single heading window, event pull loop and deterministic
teardown on the main thread, with real display and native lifetime evidence.
No GTK bridge, control listener, compiler exception or later phase was introduced.

## Latest safety gate result

Negative probes have now been executed with the pinned compiler. Private handle
construction and task transfer are rejected, but the borrowed-session escape
probe is accepted. Its binary was not run. This supersedes the earlier pending
negative-test status above. Phase 1 is blocked on this result as well as the
baseline. See [reproducer and investigation](scoped-session-blocker.md).

## Correction verification update

The user-provided correction now passes all negative reproductions with both the
installed and freshly rebuilt compilers, and the complete borrow pass/fail suites
pass. The baseline still fails on localhost HTTPS connectivity. This supersedes
the previous accepted-escape status; see `evidence/fix-verification/README.md`.
The correction is uncommitted in the sibling repository, so the strict clean-pin
checkout check still stops intentionally. Its source identity is recorded by
hashes, and the verification commands are documented in that evidence directory.

## Current committed baseline

The current clean pin is da3173c (includes bde1757). The baseline still fails in
this executor because local TCP server binding is prohibited independently of
Tarn. An isolated Python HTTP server reports EPERM on bind to 127.0.0.1. No source
change can legitimately replace enabling that environment capability. See the
latest evidence linked above; probes pass with the rebuilt pinned compiler.
