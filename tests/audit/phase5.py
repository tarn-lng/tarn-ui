"""Phase 5 layout and theme gate: measured geometry, capture fidelity and contrast."""
import base64
import hashlib
import json
import struct
import zlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/devtools"))
from reference_session import Session

EVIDENCE = ROOT / ".build/evidence/phase5-audit"
# The user's ~/.config/gtk-4.0 CSS and desktop portal settings would override
# GTK_THEME; audit native themes only. Animations are off so captures and
# inspections observe the same scroll position.
CONFIG = ROOT / ".build/audit-config"
LARGE_CONFIG = ROOT / ".build/audit-config-large"
UNICODE = "ñandú 🦊 東京都の長い行 مرحبا " * 8
MIN_CONTRAST = 4.5


def walk(node):
    yield node
    for child in node["children"]:
        yield from walk(child)


def request(session, method, **params):
    response = session.request(method, {"window_id": "main", **params})
    if "error" in response:
        raise RuntimeError(response["error"])
    return response["result"]


def decode_png(png):
    """Minimal RGBA8 decoder for the bridge's own PNG output."""
    width, height, depth, color = struct.unpack(">IIBB", png[16:26])
    if depth != 8 or color != 6:
        raise ValueError("capture is not RGBA8")
    data, offset = b"", 8
    while offset < len(png):
        size = struct.unpack(">I", png[offset:offset + 4])[0]
        if png[offset + 4:offset + 8] == b"IDAT":
            data += png[offset + 8:offset + 8 + size]
        offset += 12 + size
    raw, stride, rows, previous = zlib.decompress(data), width * 4, [], bytearray(width * 4)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - 4] if x >= 4 else 0
            b = previous[x]
            c = previous[x - 4] if x >= 4 else 0
            if kind == 1: line[x] = (line[x] + a) & 255
            elif kind == 2: line[x] = (line[x] + b) & 255
            elif kind == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        previous = line
    return width, height, rows


def luminance(pixel):
    def channel(value):
        value /= 255
        return value / 12.92 if value <= 0.03928 else ((value + 0.055) / 1.055) ** 2.4
    r, g, b = pixel[:3]
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)


def region_contrast(rows, bounds, scale, height):
    """Darkest vs lightest opaque pixel inside a visible text allocation."""
    x0, y0 = int(bounds["x"] * scale), int(bounds["y"] * scale)
    x1 = int((bounds["x"] + bounds["width"]) * scale)
    y1 = min(int((bounds["y"] + bounds["height"]) * scale), height)
    values = [luminance(rows[y][x * 4:x * 4 + 4]) for y in range(y0, y1) for x in range(x0, x1)
              if rows[y][x * 4 + 3] == 255]
    if not values:
        return None
    return (max(values) + 0.05) / (min(values) + 0.05)


def measure(name, rows, text, expanded, theme, config=CONFIG):
    with Session(binary=ROOT / ".build/navigation-dev/navigation",
                 allow=("inspect", "click", "fill", "screenshot", "close"),
                 allow_text=True, allow_capture=True,
                 env={"GTK_THEME": theme, "G_DEBUG": "fatal-warnings", "XDG_CONFIG_HOME": str(config),
                      "GDK_DEBUG": "no-portals"}) as session:
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
        snapshot = request(session, "ui.inspect")
        (EVIDENCE / f"{name}.inspect.json").write_text(json.dumps(snapshot, ensure_ascii=False, indent=2) + "\n")
        request(session, "ui.close")
        if session.process.wait(timeout=3) != 0:
            raise RuntimeError("application exit")
    if session.forced_cleanup or session.stderr:
        raise RuntimeError(f"unclean audit session: {session.stderr!r}")
    window = snapshot["root"]["bounds"]
    scale = window["scale"]
    png_width, png_height, pixels = decode_png(png)
    nodes = list(walk(snapshot["root"]))
    view = {node["id"]: node for node in nodes if node["stable_id"]}
    opaque = sum(line[x * 4 + 3] == 255 for line in pixels for x in range(png_width))
    scroll = next(node for node in nodes if node["native_type"] == "GtkScrolledWindow")
    scrolled = {id(node) for node in walk(scroll)}
    contrasts = []
    for node in nodes:
        bounds = node["bounds"]
        if node["native_type"] != "GtkLabel" or not node["enabled"] or not bounds or not node.get("text"):
            continue
        # Measure only text fully visible in the window and, for page content,
        # inside the scrolled viewport rather than clipped above or below it.
        clip = scroll["bounds"] if id(node) in scrolled else window
        if bounds["y"] < clip["y"] or bounds["y"] + bounds["height"] > clip["y"] + clip["height"]:
            continue
        ratio = region_contrast(pixels, bounds, scale, png_height)
        contrasts.append({"id": node["id"], "contrast": None if ratio is None else round(ratio, 2)})
    widest = max((node["bounds"]["x"] + node["bounds"]["width"] for node in nodes if node["bounds"]), default=0)
    return {"scenario": name, "theme": theme, "rows": rows, "expanded": expanded,
            "window": window, "list": view["task-list"]["bounds"], "back": view["tasks-back"]["bounds"],
            "heading": view["heading"]["bounds"], "png_sha256": capture["sha256"],
            "capture_pixels": {"width": png_width, "height": png_height},
            "capture_opaque_fraction": round(opaque / (png_width * png_height), 4),
            "widest_control_right_edge": widest,
            "native_scroll_containers": sum(node["native_type"] == "GtkScrolledWindow" for node in nodes),
            "text_contrast": contrasts}


def main():
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    for config, extra in ((CONFIG, ""), (LARGE_CONFIG, "gtk-font-name=Sans 20\n")):
        (config / "gtk-4.0").mkdir(parents=True, exist_ok=True)
        (config / "gtk-4.0/settings.ini").write_text("[Settings]\ngtk-enable-animations=false\n" + extra)
    monitor_data = json.loads((EVIDENCE / "monitors.json").read_text())
    monitors = monitor_data["monitors"]
    if not monitors:
        raise RuntimeError("no monitor geometry available")
    max_width = max(monitor["width"] for monitor in monitors)
    max_height = max(monitor["height"] for monitor in monitors)
    scenarios = [measure("short-light", 3, None, True, "Adwaita"),
                 measure("short-dark", 3, None, True, "Adwaita:dark"),
                 measure("short-high-contrast", 3, None, True, "HighContrast"),
                 measure("short-high-contrast-inverse", 3, None, True, "HighContrastInverse"),
                 measure("large-text", 3, None, True, "Adwaita", LARGE_CONFIG),
                 measure("full-list", 32, None, True, "Adwaita"),
                 measure("long-label", 1, "W" * 256, False, "Adwaita"),
                 measure("long-unicode", 1, UNICODE[:256], False, "Adwaita")]
    findings = []
    for scenario in scenarios:
        bounds = scenario["window"]
        scale = bounds["scale"]
        name = scenario["scenario"]
        scenario["capture_matches_window_allocation"] = (
            scenario["capture_pixels"]["width"] == bounds["width"] * scale and
            scenario["capture_pixels"]["height"] == bounds["height"] * scale)
        if bounds["width"] > max_width or bounds["height"] > max_height:
            findings.append({"scenario": name, "code": "WINDOW_EXCEEDS_MONITOR",
                             "width": bounds["width"], "height": bounds["height"]})
        if scenario["widest_control_right_edge"] > bounds["width"]:
            findings.append({"scenario": name, "code": "HORIZONTAL_OVERFLOW",
                             "right_edge": scenario["widest_control_right_edge"]})
        if not scenario["native_scroll_containers"]:
            findings.append({"scenario": name, "code": "NO_SCROLL_CONTAINER"})
        if not scenario["capture_matches_window_allocation"]:
            findings.append({"scenario": name, "code": "CAPTURE_SIZE_MISMATCH"})
        if scenario["capture_opaque_fraction"] < 0.99:
            findings.append({"scenario": name, "code": "CAPTURE_TRANSPARENT",
                             "opaque_fraction": scenario["capture_opaque_fraction"]})
        if not scenario["text_contrast"]:
            findings.append({"scenario": name, "code": "NO_TEXT_MEASURED"})
        for item in scenario["text_contrast"]:
            if item["contrast"] is None or item["contrast"] < MIN_CONTRAST:
                findings.append({"scenario": name, "code": "LOW_TEXT_CONTRAST", **item})
    base = next(s for s in scenarios if s["scenario"] == "short-light")
    large = next(s for s in scenarios if s["scenario"] == "large-text")
    if large["heading"]["height"] <= base["heading"]["height"]:
        findings.append({"scenario": "large-text", "code": "FONT_SETTING_NOT_APPLIED"})
    report = {"abi": 12, **monitor_data, "min_text_contrast": MIN_CONTRAST, "scenarios": scenarios,
              "findings": findings, "layout_gate_passed": not findings,
              "note": ("Contrast is the darkest/lightest opaque pixel ratio inside each enabled text "
                       "allocation; it measures rendered legibility, not a screen reader or WCAG audit.")}
    (EVIDENCE / "layout.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report, ensure_ascii=False))
    return 0 if not findings else 1


if __name__ == "__main__":
    sys.exit(main())
