"""Real native views, application reducers and acknowledged development events."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/devtools"))
from reference_session import Session


def nodes(node):
    yield node
    for child in node["children"]:
        yield from nodes(child)


class Reactive(unittest.TestCase):
    def start(self, mode="", *, actual=False):
        return Session(binary=ROOT / (".build/reactive-tests/example" if actual else ".build/reactive-tests/app"),
                       allow_text=True,
                       env={"TARN_UI_REACTIVE_MODE": mode, "G_DEBUG": "fatal-warnings"})

    def inspect(self, session):
        result = session.request("ui.inspect", {"window_id": "main"})["result"]
        return result, {node["id"]: node for node in nodes(result["root"]) if node["id"]}

    def click(self, session):
        return session.request("ui.click", {"window_id": "main", "element_id": "increment"})

    def close(self, session):
        self.assertTrue(session.request("ui.close", {"window_id": "main"})["result"]["closed"])
        self.assertEqual(session.process.wait(timeout=3), 0)

    def metrics(self, session):
        self.assertFalse(session.forced_cleanup)
        self.assertEqual(session.stderr, b"")
        return dict(line.split("=", 1) for line in session.stdout.decode().splitlines() if "=" in line)

    def test_actual_example_updates_counter_and_derived_heading(self):
        with self.start(actual=True) as session:
            initial, before = self.inspect(session)
            self.assertEqual(before["heading"]["text"], "Contador · par")
            reply = self.click(session)["result"]
            after, view = self.inspect(session)
            self.assertEqual(after["session_id"], initial["session_id"])
            self.assertEqual(after["revision"], reply["revision"])
            self.assertEqual(view["counter"]["text"], "1")
            self.assertEqual(view["heading"]["text"], "Contador · impar")
            self.assertEqual(view["increment"]["native_type"], "GtkButton")
            self.close(session)
        self.metrics(session)

    def test_noop_acknowledges_without_rendering_or_native_writes(self):
        with self.start("noop") as session:
            for expected in range(1, 4):
                self.assertEqual(self.click(session)["result"]["revision"], expected)
            _, view = self.inspect(session)
            self.assertEqual(view["counter"]["text"], "0")
            self.close(session)
        values = self.metrics(session)
        self.assertEqual((values["renders"], values["updates"], values["counter_writes"], values["heading_writes"]),
                         ("1", "3", "1", "1"))

    def test_grouped_initial_writes_render_only_final_state(self):
        with self.start("batch") as session:
            _, view = self.inspect(session)
            self.assertEqual(view["counter"]["text"], "3")
            self.assertEqual(view["heading"]["text"], "Contador · impar")
            self.close(session)
        values = self.metrics(session)
        self.assertEqual((values["renders"], values["state"], values["updates"]), ("1", "3", "0"))

    def test_equal_derived_view_skips_native_writes(self):
        with self.start("projection") as session:
            self.click(session)
            _, view = self.inspect(session)
            self.assertEqual(view["counter"]["text"], "0")
            self.click(session)
            _, view = self.inspect(session)
            self.assertEqual(view["counter"]["text"], "1")
            self.close(session)
        values = self.metrics(session)
        self.assertEqual((values["renders"], values["counter_writes"], values["heading_writes"]), ("3", "2", "1"))

    def test_reducer_error_and_overflow_do_not_acknowledge_success(self):
        for mode, code, initial in [("error", 99, "0"), ("overflow", 9, str(2**63 - 1))]:
            with self.subTest(mode=mode):
                with self.start(mode) as session:
                    reply = self.click(session)
                    self.assertIn("error", reply)
                    self.assertEqual(reply["error"]["details"]["application_code"], code)
                    self.assertEqual(session.process.wait(timeout=3), 1)
                values = self.metrics(session)
                self.assertEqual((values["state"], values["renders"]), (initial, "1"))

    def test_view_failure_preserves_pending_state_and_cleans_up(self):
        with self.start("badheading") as session:
            reply = self.click(session)
            self.assertIn("error", reply)
            self.assertEqual(reply["error"]["details"]["application_code"], 1)
            self.assertEqual(session.process.wait(timeout=3), 1)
        values = self.metrics(session)
        self.assertEqual((values["state"], values["dirty"]), ("1", "1"))


if __name__ == "__main__":
    unittest.main()
