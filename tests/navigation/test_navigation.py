"""Real GTK pages, scoped action delivery and retained application state."""
import base64
import hashlib
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/devtools"))
from reference_session import Session


def walk(node):
    yield node
    for child in node["children"]:
        yield from walk(child)


class Navigation(unittest.TestCase):
    def start(self, fixture_mode=None, **options):
        binary = ".build/navigation-dev/navigation" if fixture_mode is None else ".build/navigation-tests/views"
        env = {"G_DEBUG": "fatal-warnings", "TARN_UI_VIEW_MODE": fixture_mode or ""}
        env.update(options.pop("env", {}))
        return Session(binary=ROOT / binary, allow_text=options.pop("allow_text", True),
                       env=env, **options)

    def inspect(self, session):
        result = session.request("ui.inspect", {"window_id": "main"})["result"]
        return result, {node["id"]: node for node in walk(result["root"]) if node["stable_id"]}

    def click(self, session, key):
        return session.request("ui.click", {"window_id": "main", "element_id": key})

    def close(self, session):
        self.assertTrue(session.request("ui.close", {"window_id": "main"})["result"]["closed"])
        self.assertEqual(session.process.wait(timeout=3), 0)

    def clean(self, session):
        self.assertFalse(session.forced_cleanup)
        self.assertEqual(session.stderr, b"")

    def test_conditional_mixed_subtree_preserves_model_and_removes_controls(self):
        with self.start(allow=("inspect", "click", "fill", "close", "screenshot"), allow_capture=True) as session:
            self.click(session, "open-tasks")
            _, view = self.inspect(session)
            self.assertEqual(view["options-state"]["text"], "Opciones cerradas")
            self.assertNotIn("options-content", view)
            self.click(session, "show-options")
            _, view = self.inspect(session)
            self.assertEqual(view["options-title"]["type"], "heading")
            self.assertEqual(view["options-note"]["accessibility_label"], "Nota de la lista")
            self.assertEqual(view["options-note"]["parent_id"], "options-content")
            self.assertEqual(view["options-clear"]["parent_id"], "options-actions")
            self.fill(session, "Una nota con acentos: café", "options-note")
            _, view = self.inspect(session)
            self.assertEqual(view["options-preview"]["text"], "Nota: Una nota con acentos: café")
            self.assertTrue(any(node["focused"] for node in walk(view["options-note"])))
            self.click(session, "tasks-back")
            self.click(session, "open-tasks")
            _, view = self.inspect(session)
            self.assertEqual(view["options-note"]["text"], "Una nota con acentos: café")
            result = session.request("ui.screenshot", {"window_id": "main"})["result"]
            (ROOT / ".build/navigation-tests/options.png").write_bytes(base64.b64decode(result["data_base64"]))
            self.click(session, "options-close")
            _, view = self.inspect(session)
            self.assertEqual(view["options-state"]["text"], "Opciones cerradas")
            self.assertNotIn("options-content", view)
            self.assertNotIn("options-note", view)
            self.assertNotIn("options-actions", view)
            self.assertEqual(self.fill(session, "No existe", "options-note")["error"]["code"], "NOT_FOUND")
            self.assertEqual(self.click(session, "options-clear")["error"]["code"], "NOT_FOUND")
            for _ in range(5):
                self.click(session, "show-options")
                _, view = self.inspect(session)
                self.assertEqual(view["options-note"]["text"], "Una nota con acentos: café")
                self.click(session, "options-close")
            self.click(session, "show-options")
            self.click(session, "options-clear")
            _, view = self.inspect(session)
            self.assertEqual(view["options-note"]["text"], "")
            self.assertEqual(view["options-preview"]["text"], "Nota: ")
            self.close(session)
        self.clean(session)

    def test_dynamic_list_add_edit_reorder_remove_and_navigation(self):
        with self.start(allow=("inspect", "click", "fill", "close", "screenshot"), allow_capture=True) as session:
            self.click(session, "open-tasks")
            _, view = self.inspect(session)
            self.assertTrue(view["tasks"]["visible"])
            self.assertFalse(view["task-add"]["enabled"])
            for text in ("Primero", "Segundo", "Tercero"):
                self.fill(session, text, "task-input")
                self.click(session, "task-add")
            _, view = self.inspect(session)
            self.assertEqual([node["id"] for node in view["task-list"]["children"]], ["task-1", "task-2", "task-3"])
            self.assertEqual(view["task-input"]["text"], "")
            self.click(session, "task-3")
            self.assertIn("result", self.click(session, "task-up"))
            _, view = self.inspect(session)
            self.assertEqual([node["id"] for node in view["task-list"]["children"]], ["task-1", "task-3", "task-2"])
            self.fill(session, "Tercero editado", "task-input")
            self.click(session, "task-edit")
            _, view = self.inspect(session)
            self.assertEqual(view["task-3"]["text"], "✓ Tercero editado")
            self.click(session, "tasks-back")
            self.assertEqual(self.click(session, "task-3")["error"]["code"], "HIDDEN")
            self.click(session, "open-tasks")
            _, view = self.inspect(session)
            self.assertEqual(view["task-3"]["text"], "✓ Tercero editado")
            result = session.request("ui.screenshot", {"window_id": "main"})["result"]
            (ROOT / ".build/navigation-tests/tasks.png").write_bytes(base64.b64decode(result["data_base64"]))
            self.click(session, "task-remove")
            self.assertEqual(self.click(session, "task-3")["error"]["code"], "NOT_FOUND")
            _, view = self.inspect(session)
            self.assertEqual([node["id"] for node in view["task-list"]["children"]], ["task-1", "task-2"])
            self.assertEqual(view["tasks-count"]["text"], "2 elementos")
            self.assertFalse(view["task-remove"]["enabled"])
            for key in ("task-1", "task-2"):
                self.click(session, key)
                self.click(session, "task-remove")
            _, view = self.inspect(session)
            self.assertEqual(view["task-list"]["children"], [])
            self.assertEqual(view["tasks-count"]["text"], "0 elementos")
            self.fill(session, "Nueva identidad", "task-input")
            self.click(session, "task-add")
            _, view = self.inspect(session)
            self.assertIn("task-4", view)
            self.assertNotIn("task-1", view)
            self.close(session)
        self.clean(session)

    def fill(self, session, text, key="name-input"):
        return session.request("ui.fill", {"window_id": "main", "element_id": key, "text": text})

    def test_real_form_fill_save_navigation_and_reset(self):
        with self.start(allow=("inspect", "click", "fill", "close")) as session:
            self.assertEqual(self.fill(session, "  Ana María  ")["result"]["revision"], 1)
            _, view = self.inspect(session)
            self.assertEqual(view["name-input"]["text"], "  Ana María  ")
            self.assertTrue(view["save-name"]["enabled"])
            self.assertTrue(view["reset-name"]["enabled"])
            self.assertTrue(any(node["focused"] for node in walk(view["name-input"])))
            self.assertEqual(view["details-title"]["text"], "Resumen del contador")
            self.click(session, "save-name")
            _, view = self.inspect(session)
            self.assertEqual(view["name-input"]["text"], "Ana María")
            self.assertEqual(view["details-title"]["text"], "Resumen de Ana María")
            self.assertFalse(view["save-name"]["enabled"])
            self.assertFalse(view["reset-name"]["enabled"])
            self.click(session, "open-details")
            self.assertEqual(self.fill(session, "Oculto")["error"]["code"], "HIDDEN")
            self.click(session, "go-back")
            self.fill(session, "Luis")
            self.click(session, "reset-name")
            _, view = self.inspect(session)
            self.assertEqual(view["name-input"]["text"], "Ana María")
            self.assertFalse(view["save-name"]["enabled"])
            self.fill(session, "   ")
            _, view = self.inspect(session)
            self.assertFalse(view["save-name"]["enabled"])
            self.assertIn("espacios", view["name-feedback"]["text"])
            self.click(session, "reset-name")
            self.close(session)
        self.clean(session)

    def test_fill_validates_input_and_acknowledges_equal_and_empty_values(self):
        with self.start(allow=("inspect", "fill", "close")) as session:
            for revision, text in enumerate(("", "", "é" * 256, "", "Nuevo", "Nuevo"), 1):
                self.assertEqual(self.fill(session, text)["result"]["revision"], revision)
                _, view = self.inspect(session)
                self.assertEqual(view["name-input"]["text"], text)
            for text in ("x" * 257, "dos\nlíneas", "retorno\rcarro", 42, None):
                self.assertEqual(self.fill(session, text)["error"]["code"], "INVALID_REQUEST")
            self.assertEqual(self.fill(session, "bad", "home-add")["error"]["code"], "WRONG_ELEMENT_TYPE")
            self.assertEqual(self.fill(session, "bad", "missing")["error"]["code"], "NOT_FOUND")
            _, view = self.inspect(session)
            self.assertEqual(view["name-input"]["text"], "Nuevo")
            self.close(session)
        self.clean(session)

    def test_fill_pipeline_is_ordered_and_closed_fields_reject_writes(self):
        with self.start(allow=("inspect", "click", "fill", "close")) as session:
            records = [
                ("ui.fill", {"window_id": "main", "element_id": "name-input", "text": "Primero"}),
                ("ui.fill", {"window_id": "main", "element_id": "name-input", "text": "Último"}),
                ("ui.click", {"window_id": "main", "element_id": "save-name"}),
                ("ui.inspect", {"window_id": "main"}),
                ("ui.close", {"window_id": "main"}),
                ("ui.fill", {"window_id": "main", "element_id": "name-input", "text": "Tarde"}),
            ]
            session.sock.sendall(b"".join(json.dumps({"version": 1, "request_id": str(i), "method": method,
                                                    "params": params}).encode() + b"\n"
                                           for i, (method, params) in enumerate(records)))
            replies = [session.receive() for _ in records]
            self.assertEqual([reply["request_id"] for reply in replies], list(map(str, range(6))))
            self.assertEqual([reply["result"]["revision"] for reply in replies[:3]], [1, 2, 3])
            view = {node["id"]: node for node in walk(replies[3]["result"]["root"]) if node["stable_id"]}
            self.assertEqual(view["details-title"]["text"], "Resumen de Último")
            self.assertEqual(replies[5]["error"]["code"], "WINDOW_CLOSED")
            self.assertEqual(session.process.wait(timeout=3), 0)
        self.clean(session)

    def test_fill_rejects_disabled_entries(self):
        with self.start(fixture_mode="disabled-input", allow=("inspect", "fill", "close")) as session:
            self.assertEqual(self.fill(session, "No", "input")["error"]["code"], "DISABLED")
            _, view = self.inspect(session)
            self.assertEqual(view["input"]["text"], "")
            self.close(session)
        self.clean(session)

    def test_fill_application_error_is_not_acknowledged_as_success(self):
        with self.start(fixture_mode="fill-error", allow=("fill", "close")) as session:
            reply = self.fill(session, "Error del modelo", "input")
            self.assertEqual(reply["error"]["code"], "NATIVE_ERROR")
            self.assertEqual(reply["error"]["details"]["application_code"], 99)
            self.assertEqual(session.process.wait(timeout=3), 1)
        self.clean(session)

    def test_fill_requires_its_own_grant_and_does_not_grant_text_reading(self):
        with self.start() as session:
            self.assertEqual(self.fill(session, "Sin permiso")["error"]["code"], "UNAUTHORIZED")
            self.close(session)
        self.clean(session)
        with self.start(allow=("inspect", "fill", "close"), allow_text=False) as session:
            self.assertIn("result", self.fill(session, "Dato privado"))
            _, view = self.inspect(session)
            self.assertIsNone(view["name-input"]["text"])
            self.assertIsNone(view["name-input"]["accessibility_label"])
            self.close(session)
        self.clean(session)

    def test_form_save_commits_draft_and_disables_duplicate_submission(self):
        with self.start(fixture_mode="form") as session:
            _, view = self.inspect(session)
            self.assertFalse(view["save"]["enabled"])
            self.assertEqual(self.click(session, "save")["error"]["code"], "DISABLED")
            self.click(session, "trigger")
            deadline = time.monotonic() + 3
            while True:
                _, view = self.inspect(session)
                if view["save"]["enabled"]:
                    break
                self.assertLess(time.monotonic(), deadline)
            self.assertEqual(view["value"]["text"], "")
            self.click(session, "save")
            _, view = self.inspect(session)
            self.assertEqual(view["value"]["text"], "Ana María")
            self.assertFalse(view["save"]["enabled"])
            self.assertEqual(self.click(session, "save")["error"]["code"], "DISABLED")
            self.close(session)
        self.clean(session)

    def test_form_reset_restores_field_and_saved_model(self):
        with self.start(fixture_mode="form") as session:
            self.click(session, "trigger")
            deadline = time.monotonic() + 3
            while True:
                _, view = self.inspect(session)
                if view["save"]["enabled"]:
                    break
                self.assertLess(time.monotonic(), deadline)
            self.click(session, "reset")
            _, view = self.inspect(session)
            self.assertEqual(view["input"]["text"], "")
            self.assertEqual(view["value"]["text"], "")
            self.assertFalse(view["save"]["enabled"])
            self.click(session, "trigger")
            deadline = time.monotonic() + 3
            while True:
                _, view = self.inspect(session)
                if view["save"]["enabled"]:
                    break
                self.assertLess(time.monotonic(), deadline)
            self.click(session, "save")
            self.click(session, "trigger")
            deadline = time.monotonic() + 3
            while True:
                _, view = self.inspect(session)
                if view["save"]["enabled"]:
                    break
                self.assertLess(time.monotonic(), deadline)
            self.click(session, "reset")
            _, view = self.inspect(session)
            self.assertEqual(view["input"]["text"], "Ana María")
            self.assertEqual(view["value"]["text"], "Ana María")
            self.assertFalse(view["save"]["enabled"])
            self.close(session)
        self.clean(session)

    def test_input_snapshots_reach_tarn_and_derive_visible_text(self):
        with self.start(fixture_mode="input") as session:
            deadline = time.monotonic() + 3
            while True:
                _, view = self.inspect(session)
                if view["value"]["text"] == "Ana María":
                    break
                self.assertLess(time.monotonic(), deadline)
            self.assertEqual(view["input"]["text"], "Ana María")
            self.close(session)
        self.clean(session)

    def test_presentation_and_focus_restore(self):
        with self.start(allow=("inspect", "click", "close", "screenshot"), allow_capture=True) as session:
            session.request("ui.screenshot", {"window_id": "main"})
            _, view = self.inspect(session)
            self.assertTrue(view["home-add"]["focused"])
            self.assertEqual(view["home-title"]["type"], "heading")
            self.assertEqual(view["home-value"]["parent_id"], "home-summary")
            self.assertIn("title-1", view["home-value"]["style_classes"])
            self.assertIn("suggested-action", view["home-add"]["style_classes"])
            self.assertNotIn("suggested-action", view["open-details"]["style_classes"])
            self.assertEqual(view["home-add"]["accessibility_label"], "Incrementar el contador")
            left, right = view["home-add"]["bounds"], view["open-details"]["bounds"]
            self.assertIsNotNone(left)
            self.assertIsNotNone(right)
            self.assertEqual(left["y"], right["y"])
            self.assertLessEqual(left["x"] + left["width"], right["x"])
            self.click(session, "open-details")
            _, view = self.inspect(session)
            self.assertTrue(view["details-add"]["focused"])
            self.click(session, "go-back")
            _, view = self.inspect(session)
            self.assertTrue(view["open-details"]["focused"])
            self.click(session, "home-add")
            _, view = self.inspect(session)
            self.assertTrue(view["home-add"]["focused"])
            self.close(session)
        self.clean(session)

    def test_accessible_metadata_respects_text_permission(self):
        with self.start(allow_text=False) as session:
            _, view = self.inspect(session)
            for node in view.values():
                self.assertIsNone(node["text"])
                self.assertIsNone(node["accessibility_label"])
                self.assertIsNone(node["accessibility_description"])
            self.close(session)
        self.clean(session)

    def test_native_light_and_dark_themes(self):
        captures = []
        for theme in ("Adwaita", "Adwaita:dark"):
            with self.start(env={"GTK_THEME": theme}, allow=("inspect", "close", "screenshot"), allow_capture=True) as session:
                result = session.request("ui.screenshot", {"window_id": "main"})["result"]
                captures.append(result["sha256"])
                self.close(session)
            self.clean(session)
        self.assertNotEqual(*captures)

    def test_shared_state_and_hidden_targets(self):
        with self.start() as session:
            initial, view = self.inspect(session)
            self.assertTrue(view["home"]["visible"])
            self.assertFalse(view["details"]["visible"])
            self.assertEqual(view["home-value"]["text"], "0")
            self.assertEqual(self.click(session, "details-add")["error"]["code"], "HIDDEN")
            self.assertEqual(self.click(session, "home-add")["result"]["revision"], 1)
            self.assertEqual(self.click(session, "open-details")["result"]["revision"], 2)
            snapshot, view = self.inspect(session)
            self.assertEqual(snapshot["session_id"], initial["session_id"])
            self.assertTrue(view["details"]["visible"])
            self.assertFalse(view["home"]["visible"])
            self.assertEqual(view["details-value"]["text"], "1")
            self.assertEqual(view["details-parity"]["text"], "El valor es impar")
            self.assertEqual(view["details-add"]["native_type"], "GtkButton")
            self.assertEqual(self.click(session, "home-add")["error"]["code"], "HIDDEN")
            self.click(session, "details-add")
            self.click(session, "go-back")
            _, view = self.inspect(session)
            self.assertTrue(view["home"]["visible"])
            self.assertEqual(view["home-value"]["text"], "2")
            self.assertEqual(view["details-value"]["text"], "2")
            self.assertEqual(view["details-parity"]["text"], "El valor es par")
            self.close(session)
        self.clean(session)

    def test_repeated_navigation_preserves_keys_and_state(self):
        with self.start() as session:
            _, before = self.inspect(session)
            for _ in range(10):
                self.click(session, "home-add")
                self.click(session, "open-details")
                self.click(session, "go-back")
            snapshot, after = self.inspect(session)
            self.assertEqual(set(before), set(after))
            self.assertEqual(after["home-value"]["text"], "10")
            self.assertEqual(snapshot["revision"], 30)
            self.close(session)
        self.clean(session)

    def test_capture_matches_active_page(self):
        with self.start(allow=("inspect", "click", "close", "screenshot"), allow_capture=True) as session:
            captures = []
            for name in ("home", "details"):
                if name == "details":
                    self.click(session, "home-add")
                    self.click(session, "open-details")
                result = session.request("ui.screenshot", {"window_id": "main"})["result"]
                png = base64.b64decode(result["data_base64"], validate=True)
                self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
                self.assertEqual(hashlib.sha256(png).hexdigest(), result["sha256"])
                (ROOT / f".build/navigation-tests/{name}.png").write_bytes(png)
                captures.append(png)
            self.assertNotEqual(*captures)
            self.close(session)
        self.clean(session)

    def test_navigation_pipeline_closes_in_order(self):
        with self.start() as session:
            requests = [("ui.click", {"window_id": "main", "element_id": "open-details"}),
                        ("ui.click", {"window_id": "main", "element_id": "go-back"}),
                        ("ui.close", {"window_id": "main"}),
                        ("ui.inspect", {"window_id": "main"})]
            records = [{"version": 1, "request_id": str(index), "method": method, "params": params}
                       for index, (method, params) in enumerate(requests)]
            session.sock.sendall(b"".join(json.dumps(record).encode() + b"\n" for record in records))
            replies = [session.receive() for _ in records]
            self.assertEqual([reply["request_id"] for reply in replies], ["0", "1", "2", "3"])
            self.assertEqual([reply["result"]["revision"] for reply in replies[:2]], [1, 2])
            self.assertTrue(replies[2]["result"]["closed"])
            self.assertEqual(replies[3]["error"]["code"], "WINDOW_CLOSED")
            self.assertEqual(session.process.wait(timeout=3), 0)
        self.clean(session)

    def test_component_actions_keep_operation_grants(self):
        with self.start(allow=("inspect", "close")) as session:
            self.assertEqual(self.click(session, "open-details")["error"]["code"], "UNAUTHORIZED")
            _, view = self.inspect(session)
            self.assertTrue(view["home"]["visible"])
            self.close(session)
        self.clean(session)

    def test_equal_view_skips_all_native_property_and_route_calls(self):
        with self.start(fixture_mode="noop") as session:
            for revision in range(1, 4):
                self.assertEqual(self.click(session, "trigger")["result"]["revision"], revision)
            self.close(session)
        self.clean(session)
        values = dict(line.split("=", 1) for line in session.stdout.decode().splitlines() if "=" in line)
        self.assertEqual(values, {"text_calls": "1", "page_calls": "1"})

    def test_view_errors_never_acknowledge_success_and_release_the_tree(self):
        for mode, code in (("duplicate", 1), ("duplicate-enabled", 1), ("duplicate-list", 1), ("list-collision", 1), ("tree-duplicate", 1), ("tree-collision", 1), ("tree-kind", 1), ("tree-depth", 1), ("route", 1), ("error", 99)):
            with self.subTest(mode=mode):
                with self.start(fixture_mode=mode) as session:
                    reply = self.click(session, "trigger")
                    self.assertEqual(reply["error"]["details"]["application_code"], code)
                    self.assertEqual(session.process.wait(timeout=3), 1)
                self.clean(session)
                self.assertIn(b"text_calls=1", session.stdout)

    def test_mcp_launcher_drives_navigation_and_back(self):
        process = subprocess.Popen([
            str(ROOT / ".build/launcher/tarn-ui"), "--mode", "mcp",
            "--binary", str(ROOT / ".build/navigation-dev/navigation"),
            "--library-path", str(ROOT / ".build/navigation-dev"),
            "--allow", "inspect", "click", "fill", "close", "--allow-text",
        ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        buffer = bytearray()
        sequence = 0

        def request(method, params):
            nonlocal sequence
            sequence += 1
            process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": sequence,
                                            "method": method, "params": params}).encode() + b"\n")
            process.stdin.flush()
            deadline = time.monotonic() + 10
            while b"\n" not in buffer:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not select.select([process.stdout], [], [], max(0, remaining))[0]:
                    raise TimeoutError("MCP response deadline")
                chunk = os.read(process.stdout.fileno(), 8192)
                if not chunk:
                    raise EOFError("MCP stdout closed")
                buffer.extend(chunk)
                if len(buffer) > 8 * 1024 * 1024:
                    raise ValueError("MCP response budget")
            line, _, rest = buffer.partition(b"\n")
            buffer[:] = rest
            reply = json.loads(line)
            self.assertEqual(reply["id"], sequence)
            return reply["result"]

        def call(name, arguments=None):
            result = request("tools/call", {"name": name, "arguments": arguments or {}})
            self.assertFalse(result["isError"], result)
            return result["structuredContent"]

        try:
            request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                   "clientInfo": {"name": "navigation-test", "version": "1"}})
            process.stdin.write(b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
            process.stdin.flush()
            listing = request("tools/list", {})
            fill_tool = next(tool for tool in listing["tools"] if tool["name"] == "ui.fill")
            self.assertEqual(fill_tool["inputSchema"]["properties"]["text"]["maxLength"], 256)
            call("ui.start")
            call("ui.fill", {"element_id": "name-input", "text": "Desde MCP"})
            call("ui.click", {"element_id": "save-name"})
            call("ui.click", {"element_id": "home-add"})
            call("ui.click", {"element_id": "open-details"})
            visible = {node["id"]: node for node in walk(call("ui.inspect")["root"]) if node["stable_id"]}
            self.assertTrue(visible["details"]["visible"])
            self.assertEqual(visible["details-value"]["text"], "1")
            self.assertEqual(visible["details-title"]["text"], "Resumen de Desde MCP")
            call("ui.click", {"element_id": "go-back"})
            visible = {node["id"]: node for node in walk(call("ui.inspect")["root"]) if node["stable_id"]}
            self.assertTrue(visible["home"]["visible"])
            self.assertEqual(visible["home-value"]["text"], "1")
            self.assertTrue(call("ui.close")["closed"])
            process.stdin.close()
            self.assertEqual(process.wait(timeout=5), 0)
            self.assertEqual(process.stderr.read(), b"")
            self.assertEqual(bytes(buffer) + process.stdout.read(), b"")
        finally:
            if not process.stdin.closed:
                process.stdin.close()
            if process.poll() is None:
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
            process.stdout.close()
            process.stderr.close()


if __name__ == "__main__":
    unittest.main()
