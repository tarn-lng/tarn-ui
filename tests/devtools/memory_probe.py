"""One native transport lifecycle with sanitizers. Preserve the native exit code."""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/devtools"))
from reference_session import Session

asan = subprocess.check_output(["cc", "-print-file-name=libasan.so"], text=True).strip()
binary = ROOT / ".build/devtools-memory/counter"
evidence = ROOT / ".build/evidence/phase3"
evidence.mkdir(parents=True, exist_ok=True)
with Session(binary=binary, allow_text=True,
             env={"LD_PRELOAD": asan, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                  "UBSAN_OPTIONS": "halt_on_error=1", "G_DEBUG": "fatal-warnings"}) as session:
    click = session.request("ui.click", {"window_id": "main", "element_id": "increment"})
    assert click["result"]["revision"] == 1
    snapshot = session.request("ui.inspect", {"window_id": "main"})["result"]

    def nodes(node):
        yield node
        for child in node["children"]:
            yield from nodes(child)

    assert next(n["text"] for n in nodes(snapshot["root"]) if n["id"] == "counter") == "1"
    assert session.request("ui.close", {"window_id": "main"})["result"]["closed"]
    native_exit = session.process.wait(timeout=5)
assert not session.forced_cleanup
assert b"clean" in session.stdout
(evidence / "memory-lsan.log").write_bytes(session.stderr)
(evidence / "memory.exit").write_text(str(native_exit) + "\n")
print(json.dumps({"click_inspect_close_assertions": "passed", "native_exit": native_exit,
                  "forced_cleanup": session.forced_cleanup}))
raise SystemExit(native_exit)
