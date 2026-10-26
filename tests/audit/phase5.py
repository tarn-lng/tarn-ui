"""Collect layout observations; findings are audit data, not passing assertions."""
import base64
import hashlib
import json
import struct
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/devtools"))
from reference_session import Session

EVIDENCE = ROOT / ".build/evidence/phase5-audit"


def walk(node):
    yield node
    for child in node["children"]:
        yield from walk(child)


def request(session, method, **params):
    response = session.request(method, {"window_id": "main", **params})
    if "error" in response:
        raise RuntimeError(response["error"])
    return response["result"]


def measure(name, rows, text, expanded, theme):
    with Session(binary=ROOT / ".build/navigation-dev/navigation",
                 allow=("inspect", "click", "fill", "screenshot", "close"),
                 allow_text=True, allow_capture=True,
                 env={"GTK_THEME": theme, "G_DEBUG": "fatal-warnings"}) as session:
        request(session, "ui.click", element_id="open-tasks")
        for index in range(rows):
            request(session, "ui.fill", element_id="task-input", text=text or f"Elemento {index + 1}")
            request(session, "ui.click", element_id="task-add")
        if expanded:
            request(session, "ui.click", element_id="show-options")
            request(session, "ui.fill", element_id="options-note", text="Nota accesible de auditoría")
        capture = request(session, "ui.screenshot")
        png = base64.b64decode(capture["data_base64"], validate=True)
        if hashlib.sha256(png).hexdigest() != capture["sha256"]:
            raise ValueError("capture integrity")
        (EVIDENCE / f"{name}.png").write_bytes(png)
        png_width, png_height = struct.unpack(">II", png[16:24])
        if (png_width, png_height) != (capture["width"], capture["height"]):
            raise ValueError("PNG dimensions do not match capture metadata")
        snapshot = request(session, "ui.inspect")
        (EVIDENCE / f"{name}.inspect.json").write_text(json.dumps(snapshot, ensure_ascii=False, indent=2) + "\n")
        view = {node["id"]: node for node in walk(snapshot["root"]) if node["stable_id"]}
        result = {"scenario": name, "theme": theme, "rows": rows, "expanded": expanded,
                  "window": snapshot["root"]["bounds"], "list": view["task-list"]["bounds"],
                  "back": view["tasks-back"]["bounds"], "png_sha256": capture["sha256"],
                  "capture_pixels": {"width": png_width, "height": png_height},
                  "native_scroll_containers": sum(node["native_type"] == "GtkScrolledWindow" for node in walk(snapshot["root"]))}
        request(session, "ui.close")
        if session.process.wait(timeout=3) != 0:
            raise RuntimeError("application exit")
    if session.forced_cleanup or session.stderr:
        raise RuntimeError(f"unclean audit session: {session.stderr!r}")
    return result


def main():
    monitor_data = json.loads((EVIDENCE / "monitors.json").read_text())
    monitors = monitor_data["monitors"]
    if not monitors:
        raise RuntimeError("no monitor geometry available")
    max_width = max(monitor["width"] for monitor in monitors)
    max_height = max(monitor["height"] for monitor in monitors)
    scenarios = [measure("short-light", 3, None, True, "Adwaita"),
                 measure("short-dark", 3, None, True, "Adwaita:dark"),
                 measure("full-list", 32, None, True, "Adwaita"),
                 measure("long-label", 1, "W" * 256, False, "Adwaita")]
    findings = []
    for scenario in scenarios:
        bounds = scenario["window"]
        scale = bounds["scale"]
        scenario["capture_matches_window_allocation"] = (
            scenario["capture_pixels"]["width"] == bounds["width"] * scale and
            scenario["capture_pixels"]["height"] == bounds["height"] * scale)
        scenario["exceeds_every_monitor_width"] = bounds["width"] > max_width
        scenario["exceeds_every_monitor_height"] = bounds["height"] > max_height
        if scenario["exceeds_every_monitor_width"] or scenario["exceeds_every_monitor_height"]:
            findings.append({"scenario": scenario["scenario"], "code": "WINDOW_EXCEEDS_MONITOR",
                             "width": bounds["width"], "height": bounds["height"]})
    report = {"abi": 12, **monitor_data, "scenarios": scenarios, "findings": findings,
              "layout_gate_passed": not findings,
              "note": "Functional capture success does not prove controls fit the monitor or keyboard accessibility."}
    (EVIDENCE / "layout.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
