"""Native form controls driven end to end through the development protocol."""
import base64
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/devtools"))
from reference_session import Session

BINARY = ROOT / ".build/widgets-dev/widgets"
EVIDENCE = ROOT / ".build/widgets-tests"


def walk(node):
    yield node
    for child in node["children"]:
        yield from walk(child)


class WidgetTests(unittest.TestCase):
    def start(self, **options):
        config = ROOT / ".build/audit-config"
        return Session(binary=BINARY, allow=("inspect", "click", "fill", "screenshot", "close", "diagnostics"),
                       allow_text=True, allow_capture=True,
                       env={"GTK_THEME": "Adwaita", "XDG_CONFIG_HOME": str(config),
                            "GDK_DEBUG": "no-portals", "G_DEBUG": "fatal-warnings"}, **options)

    def request(self, session, method, **params):
        return session.request(method, {"window_id": "main", **params})

    def view(self, session):
        snapshot = self.request(session, "ui.inspect")["result"]
        return {node["id"]: node for node in walk(snapshot["root"]) if node["stable_id"]}

    def close(self, session):
        self.assertIn("result", self.request(session, "ui.close"))
        self.assertEqual(session.process.wait(timeout=3), 0)

    def test_native_types_roles_and_initial_values(self):
        with self.start() as session:
            view = self.view(session)
            expected = {"notify": ("GtkSwitch", "switch", "1"), "dark": ("GtkCheckButton", "checkbox", "0"),
                        "volume": ("GtkScale", "slider", "40"), "copies": ("GtkSpinButton", "spin-button", "1"),
                        "quality": ("GtkDropDown", "combo-box", "1")}
            for key, (native, role, value) in expected.items():
                self.assertEqual((view[key]["native_type"], view[key]["type"], view[key]["text"]), (native, role, value))
            self.close(session)

    def test_user_changes_reach_the_tarn_model(self):
        with self.start() as session:
            for key, value in (("volume", "75"), ("copies", "3"), ("quality", "2"), ("notify", "0"), ("dark", "1")):
                self.assertIn("result", self.request(session, "ui.fill", element_id=key, text=value), key)
            view = self.view(session)
            self.assertEqual(view["summary"]["text"],
                             "Notificaciones: no · oscuro: sí · volumen 75 · copias 3 · calidad 2")
            self.close(session)

    def test_model_writes_restore_controls_without_events(self):
        with self.start() as session:
            self.request(session, "ui.fill", element_id="volume", text="90")
            self.assertIn("result", self.request(session, "ui.click", element_id="reset"))
            view = self.view(session)
            self.assertEqual(view["volume"]["text"], "40")
            self.assertEqual(view["notify"]["text"], "1")
            self.assertEqual(self.request(session, "ui.diagnostics").get("result", {}).get("queued_events", 0), 0)
            self.close(session)

    def test_out_of_range_and_non_numeric_values_are_rejected(self):
        with self.start() as session:
            for key, value in (("copies", "500"), ("copies", "0"), ("volume", "abc"), ("quality", "3"),
                               ("dark", "2"), ("volume", "")):
                self.assertEqual(self.request(session, "ui.fill", element_id=key, text=value)["error"]["code"],
                                 "INVALID_REQUEST", (key, value))
            self.assertEqual(self.view(session)["copies"]["text"], "1")
            self.close(session)

    def test_indicators_follow_the_model_and_refuse_input(self):
        with self.start() as session:
            view = self.view(session)
            expected = {"volume-meter": ("GtkProgressBar", "progress-bar", "40"),
                        "copies-meter": ("GtkLevelBar", "meter", "1"),
                        "syncing": ("GtkSpinner", "progress-bar", "1")}
            for key, (native, role, value) in expected.items():
                self.assertEqual((view[key]["native_type"], view[key]["type"], view[key]["text"]), (native, role, value), key)
            self.request(session, "ui.fill", element_id="volume", text="75")
            self.request(session, "ui.fill", element_id="copies", text="7")
            self.request(session, "ui.fill", element_id="notify", text="0")
            view = self.view(session)
            self.assertEqual((view["volume-meter"]["text"], view["copies-meter"]["text"], view["syncing"]["text"]),
                             ("75", "7", "0"))
            for key in ("volume-meter", "copies-meter", "syncing"):
                self.assertEqual(self.request(session, "ui.fill", element_id=key, text="1")["error"]["code"],
                                 "WRONG_ELEMENT_TYPE", key)
                self.assertEqual(self.request(session, "ui.click", element_id=key)["error"]["code"],
                                 "WRONG_ELEMENT_TYPE", key)
            self.close(session)

    def test_containers_keep_state_in_the_model(self):
        with self.start() as session:
            view = self.view(session)
            self.assertEqual(view["meters"]["native_type"], "GtkBox")
            self.assertEqual((view["details"]["native_type"], view["details"]["text"]), ("GtkExpander", "0"))
            self.assertEqual((view["sections"]["native_type"], view["sections"]["text"]), ("GtkNotebook", "0"))
            self.assertNotIn("details-text", view)  # GTK 4 unparents a collapsed expander child
            self.assertTrue(view["general-text"]["visible"])
            self.assertFalse(view["about-text"]["visible"])
            # User changes arrive as actions; the model writes them back.
            self.assertIn("result", self.request(session, "ui.fill", element_id="details", text="1"))
            self.assertIn("result", self.request(session, "ui.fill", element_id="sections", text="1"))
            view = self.view(session)
            self.assertEqual((view["details"]["text"], view["sections"]["text"]), ("1", "1"))
            self.assertTrue(view["details-text"]["visible"])
            self.assertTrue(view["about-text"]["visible"])
            self.assertFalse(view["general-text"]["visible"])
            self.assertEqual(self.request(session, "ui.fill", element_id="sections", text="2")["error"]["code"],
                             "INVALID_REQUEST")
            # Reset restores both from the model without queuing events.
            self.request(session, "ui.click", element_id="reset")
            view = self.view(session)
            self.assertEqual((view["details"]["text"], view["sections"]["text"]), ("0", "0"))
            self.assertEqual(self.request(session, "ui.diagnostics").get("result", {}).get("queued_events", 0), 0)
            self.close(session)

    def test_multiline_text_round_trips_through_the_model(self):
        with self.start() as session:
            view = self.view(session)
            self.assertEqual((view["note"]["native_type"], view["note"]["type"], view["note"]["text"]),
                             ("GtkTextView", "text-box", ""))
            self.assertEqual(view["note-count"]["text"], "Nota: 0 líneas")
            text = "primera línea\nsegunda\ntercera"
            self.assertIn("result", self.request(session, "ui.fill", element_id="note", text=text))
            view = self.view(session)
            self.assertEqual((view["note"]["text"], view["note-count"]["text"]), (text, "Nota: 3 líneas"))
            # Entries stay single-line; only the text area accepts newlines.
            self.assertEqual(self.request(session, "ui.fill", element_id="copies", text="1\n2").get("error", {}).get("code"),
                             "INVALID_REQUEST")
            self.request(session, "ui.click", element_id="reset")
            self.assertEqual(self.view(session)["note"]["text"], "")
            self.assertEqual(self.request(session, "ui.diagnostics").get("result", {}).get("queued_events", 0), 0)
            self.close(session)

    def test_header_bar_buttons_and_menu(self):
        with self.start() as session:
            view = self.view(session)
            self.assertEqual(view["header-reset"]["native_type"], "GtkButton")
            self.assertEqual(view["menu"]["native_type"], "GtkMenuButton")
            self.request(session, "ui.fill", element_id="volume", text="90")
            self.assertIn("result", self.request(session, "ui.click", element_id="header-reset"))
            self.assertEqual(self.view(session)["volume"]["text"], "40")
            self.assertIn("result", self.request(session, "ui.click", element_id="menu-about"))
            self.assertEqual(self.view(session)["sections"]["text"], "1")
            self.request(session, "ui.fill", element_id="copies", text="5")
            self.assertIn("result", self.request(session, "ui.click", element_id="menu-reset"))
            view = self.view(session)
            self.assertEqual((view["copies"]["text"], view["sections"]["text"]), ("1", "0"))
            self.assertEqual(self.request(session, "ui.click", element_id="menu-missing")["error"]["code"], "NOT_FOUND")
            self.close(session)

    def test_dynamic_region_mounts_controls_containers_and_text(self):
        keys = ("extras-frame", "extras-level", "extras-meter", "extras-note", "extras-more")
        with self.start() as session:
            view = self.view(session)
            self.assertFalse(any(key in view for key in keys))
            self.assertIn("result", self.request(session, "ui.fill", element_id="extras-toggle", text="1"))
            view = self.view(session)
            self.assertEqual({key: view[key]["native_type"] for key in keys},
                             {"extras-frame": "GtkBox", "extras-level": "GtkSpinButton", "extras-meter": "GtkProgressBar",
                              "extras-note": "GtkTextView", "extras-more": "GtkExpander"})
            self.assertEqual((view["extras-level"]["text"], view["extras-meter"]["text"]), ("3", "3"))
            # Edits inside the region reach the model and flow back to every view of it.
            self.request(session, "ui.fill", element_id="extras-level", text="7")
            self.request(session, "ui.fill", element_id="extras-more", text="1")
            self.request(session, "ui.fill", element_id="extras-note", text="uno\ndos")
            view = self.view(session)
            self.assertEqual((view["extras-meter"]["text"], view["extras-more-text"]["text"]), ("7", "Nivel 7"))
            self.assertEqual((view["note"]["text"], view["details"]["text"]), ("uno\ndos", "1"))
            # Editing the static twin rewrites the region's copy without events.
            self.request(session, "ui.fill", element_id="note", text="tres")
            self.assertEqual(self.view(session)["extras-note"]["text"], "tres")
            # Turning the region off removes every node; on again restores them from the model.
            self.request(session, "ui.fill", element_id="extras-toggle", text="0")
            view = self.view(session)
            self.assertFalse(any(key in view for key in keys + ("extras-more-text",)))
            self.request(session, "ui.fill", element_id="extras-toggle", text="1")
            view = self.view(session)
            self.assertEqual((view["extras-level"]["text"], view["extras-more"]["text"], view["extras-note"]["text"]),
                             ("7", "1", "tres"))
            self.assertEqual(self.request(session, "ui.diagnostics").get("result", {}).get("queued_events", 0), 0)
            self.close(session)

    def test_capture_shows_every_control(self):
        EVIDENCE.mkdir(parents=True, exist_ok=True)
        with self.start() as session:
            capture = self.request(session, "ui.screenshot")["result"]
            (EVIDENCE / "widgets.png").write_bytes(base64.b64decode(capture["data_base64"]))
            view = self.view(session)
            for key in ("notify", "dark", "volume", "copies", "quality", "volume-meter", "copies-meter"):
                bounds = view[key]["bounds"]
                self.assertIsNotNone(bounds, key)
                self.assertLessEqual(bounds["x"] + bounds["width"], capture["width"], key)
            self.close(session)


if __name__ == "__main__":
    unittest.main(verbosity=2)
