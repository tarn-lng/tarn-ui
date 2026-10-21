# Why the HTTPS registry test fails in this executor

Verified against clean Tarn da3173c. This is an execution restriction, not a TLS
failure or compiler rejection.

1. `socket.socket(AF_INET, SOCK_STREAM)` fails immediately with EPERM. IPv6 TCP
   fails identically. This precedes bind/listen/connect and all TLS operations.
2. Replaying the exact Python fixture startup with stderr captured produces a
   traceback in `socket.py` when creating the server socket. It exits 1 and
   writes no port to stdout. Certificate loading is never reached.
3. `tools/cli/tests/packages.rs:344` discards the server's stderr. `read_line`
   at line 357 returns EOF (zero bytes), which is not checked; `port` stays empty.
   The test continues with `https://127.0.0.1:/`.
4. Independently running curl on this URL prints `failed to open socket:
   Operation not permitted` and exits 7. No TLS handshake or CA verification runs.

`CODEX_SANDBOX_NETWORK_DISABLED=1`, NoNewPrivs=1 and Seccomp=2 are observed.
No HTTP/SOCKS proxy environment variables are set. We have evidence of denied
socket creation; these observations alone do not identify the exact filter
rule responsible. No escalation or sandbox bypass was attempted.

The required environment capability is permission to create IPv4 TCP sockets
and bind/listen/connect on loopback. The upstream test already uses 127.0.0.1
and its certificate covers that IP. Disabling certificate verification,
changing DNS, installing GTK or changing Tarn borrow analysis cannot fix EPERM.

An optional improvement to the fixture is checking successful server startup,
validating a nonempty numeric port, and surfacing captured stderr on failure.
That would improve diagnostics, not make networking possible. No Tarn source
was edited in this investigation.

Logs: `socket-stages.log`, `fixture-startup.log`, `curl.stderr`, `curl.exit`.
The earlier phrase “bind fails” described the high-level HTTP server operation;
the precise failing syscall boundary is **socket creation**, before bind.

## Continuing validation

A full-suite run using `--no-fail-fast` is recorded in `full-suite.log`;
network-dependent failures extend beyond HTTPS to async TCP fixtures.
GTK's installed SDK passes header/link checks, but `gtk_init_check()` returns
false in this executor; this is a separate observed display-access limitation.
No native window was opened.

For a complete host-terminal verification, run:

```bash
bash scripts/validate-local.sh
```

This script uses the clean pinned sibling sources, runs the complete locked
suite with no test exclusions, then runs all probe scripts using the matching
compiler. Logs and exit codes go to `evidence/local-validation/`.
This host-terminal command has been prepared and syntax-checked, not claimed
as executed outside the sandbox.
