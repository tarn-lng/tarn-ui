#!/usr/bin/env python3
"""Focused executable tests. Snapshots are asserted, never saved to the log."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/devtools"))
from reference_session import Session

FIXTURE = ROOT / ".build/devtools-tests/counter"


def walk(node):
    yield node
    for child in node["children"]:
        yield from walk(child)


class Protocol(unittest.TestCase):
    def start(self, mode=None, **kwargs):
        env = {"G_DEBUG": "fatal-warnings"}
        if mode:
            env["TARN_UI_TEST_MODE"] = mode
        return Session(binary=FIXTURE, env=env, **kwargs)

    def inspect(self, s):
        reply = s.request("ui.inspect", {"window_id": "main"})
        self.assertIn("result", reply, reply)
        return reply["result"]

    def close(self, s, status=0):
        reply = s.request("ui.close", {"window_id": "main"})
        self.assertTrue(reply["result"]["closed"])
        self.assertEqual(s.process.wait(timeout=3), status)

    def error(self, reply, code):
        self.assertEqual(reply.get("error", {}).get("code"), code, reply)
        self.assertNotIn("result", reply)

    def test_acceptance_real_counter_ordered_ack_and_stale_window(self):
        # This acceptance uses the actual development example, not the fixture.
        with Session(allow_text=True) as s:
            first = self.inspect(s)
            nodes = {n["id"]: n for n in walk(first["root"])}
            self.assertEqual(first["platform"], "linux")
            self.assertEqual(first["backend"], "gtk4")
            self.assertEqual(nodes["increment"]["native_type"], "GtkButton")
            self.assertTrue(nodes["increment"]["visible"] and nodes["increment"]["enabled"])
            self.assertEqual(nodes["increment"]["parent_id"], "content")
            self.assertTrue(nodes["increment"]["children"])
            self.assertFalse(nodes["increment"]["children"][0]["stable_id"])
            self.assertGreater(nodes["counter"]["bounds"]["width"], 0)
            self.assertIsNone(nodes["counter"]["global_bounds"])
            initial = int(nodes["counter"]["text"])
            ids = [s.send("ui.click", {"window_id": "main", "element_id": "increment"}) for _ in range(3)]
            ids.append(s.send("ui.inspect", {"window_id": "main"}))
            ids.append(s.send("ui.close", {"window_id": "main"}))
            ids.append(s.send("ui.inspect", {"window_id": "main"}))
            replies = [s.receive() for _ in ids]
            self.assertEqual([r["request_id"] for r in replies], ids)
            revisions = [r["result"]["revision"] for r in replies[:3]]
            self.assertEqual(revisions, [first["revision"] + i for i in (1, 2, 3)])
            after = replies[3]["result"]
            self.assertEqual(after["session_id"], first["session_id"])
            self.assertEqual(after["revision"], revisions[-1])
            self.assertEqual(int(next(n["text"] for n in walk(after["root"]) if n["id"] == "counter")), initial + 3)
            self.assertTrue(replies[4]["result"]["closed"])
            self.error(replies[5], "WINDOW_CLOSED")
            self.assertEqual(s.process.wait(timeout=3), 0)
        self.assertFalse(s.forced_cleanup)

    def test_authorization_and_session_binding(self):
        with self.start(authorize=False) as s:
            self.error(s.request("ui.inspect", {"window_id": "main"}), "UNAUTHORIZED")
            self.error(s.request("session.authorize", {"session_id": "wrong-session"}), "UNAUTHORIZED")
            self.assertIn("result", s.request("session.authorize", {"session_id": s.ready["session_id"]}))
            self.close(s)
        self.assertFalse(s.forced_cleanup)

    def test_scope_denial(self):
        with self.start(allow=("inspect", "close")) as s:
            self.error(s.request("ui.click", {"window_id": "main", "element_id": "increment"}), "UNAUTHORIZED")
            self.close(s)

    def test_text_access_is_explicit_and_redacted(self):
        with self.start() as s:
            snapshot = self.inspect(s)
            for node in walk(snapshot["root"]):
                self.assertIsNone(node["text"])
                self.assertTrue(node["redacted"])
            self.assertNotIn("Native fixture", json.dumps(snapshot))
            self.close(s)

    def test_inspect_uses_native_getter_not_expected_state(self):
        with self.start("native-text", allow_text=True) as s:
            snapshot = self.inspect(s)
            self.assertEqual(next(n["text"] for n in walk(snapshot["root"]) if n["id"] == "counter"), "777")
            self.close(s)

    def test_targets_and_unsupported_methods(self):
        with self.start() as s:
            self.error(s.request("ui.inspect", {"window_id": "missing"}), "NOT_FOUND")
            self.error(s.request("ui.click", {"window_id": "main", "element_id": "missing"}), "NOT_FOUND")
            self.error(s.request("ui.click", {"window_id": "main", "element_id": "counter"}), "WRONG_ELEMENT_TYPE")
            self.error(s.request("ui.click", {"window_id": "main", "element_id": "increment/0"}), "WRONG_ELEMENT_TYPE")
            self.error(s.request("ui.screenshot", {"window_id": "main"}), "UNAUTHORIZED")
            for method in ("ui.type", "ui.rebuild"):
                self.error(s.request(method, {}), "UNSUPPORTED_OPERATION")
            self.close(s)

    def test_native_disabled_hidden_and_ancestor_sensitivity(self):
        for mode, code in (("disabled", "DISABLED"), ("ancestor-disabled", "DISABLED"), ("hidden", "HIDDEN")):
            with self.subTest(mode=mode), self.start(mode) as s:
                self.error(s.request("ui.click", {"window_id": "main", "element_id": "increment"}), code)
                node = next(n for n in walk(self.inspect(s)["root"]) if n["id"] == "increment")
                self.assertFalse(node["visible"] if mode == "hidden" else node["enabled"])
                self.close(s)

    def test_malformed_records_and_duplicate_members(self):
        records = [b"{bad}\n", b"[]\n", b"\xff\n", b"\x00\n", b"{}\n",
                   b'{"version":1,"version":1,"request_id":"dup","method":"ui.inspect","params":{"window_id":"main"}}\n',
                   b'{"version":1,"request_id":"x\\u0000y","method":"ui.inspect","params":{"window_id":"main"}}\n',
                   b'{"version":1,"request_id":"slash/id","method":"ui.inspect","params":{"window_id":"main"}}\n',
                   b'{"version":1,"request_id":"extra","method":"ui.inspect","params":{"window_id":"main","extra":0}}\n',
                   b'{"version":1,"request_id":"extra-root","method":"ui.inspect","params":{"window_id":"main"},"extra":0}\n',
                   b'{"version":1,"request_id":"param-dup","method":"ui.inspect","params":{"window_id":"main","window_id":"main"}}\n',
                   b'{"version":1.0,"request_id":"float","method":"ui.inspect","params":{"window_id":"main"}}\n',
                   b'{"version":1,/*comment*/"request_id":"comment","method":"ui.inspect","params":{"window_id":"main"}}\n',
                   b"[" * 65 + b"]" * 65 + b"\n"]
        with self.start() as s:
            for record in records:
                s.sock.sendall(record)
                self.error(s.receive(), "INVALID_REQUEST")
            self.error(s.request("ui.inspect", {"window_id": "main"}, version=2), "UNSUPPORTED_VERSION")
            self.close(s)

    def test_fragmented_request_and_reused_id(self):
        with self.start() as s:
            record = b'{"version":1,"request_id":"fragment","method":"ui.inspect","params":{"window_id":"main"}}\n'
            for cut in (record[:7], record[7:31], record[31:]):
                s.sock.sendall(cut)
            self.assertIn("result", s.receive())
            s.sock.sendall(record)
            self.error(s.receive(), "INVALID_REQUEST")
            self.close(s)

    def test_record_and_input_queue_limits(self):
        for payload, code in ((b"x" * (1024 * 1024 + 1) + b"\n", "INVALID_REQUEST"),
                              (b"{}\n" * 33, "QUEUE_FULL")):
            with self.subTest(code=code), self.start() as s:
                try:
                    s.sock.sendall(payload)
                except BrokenPipeError:
                    pass
                self.error(s.receive(), code)
                with self.assertRaises(EOFError):
                    s.receive()
            # Terminal channel failures intentionally cancel the harness process.
            self.assertTrue(s.forced_cleanup)

    def test_handler_error_never_acknowledges_success(self):
        with self.start("handler-error") as s:
            self.error(s.request("ui.click", {"window_id": "main", "element_id": "increment"}), "NATIVE_ERROR")
            self.assertEqual(s.process.wait(timeout=3), 1)
        self.assertFalse(s.forced_cleanup)
        self.assertIn(b"clean", s.stdout)

    def test_close_during_pending_action(self):
        for mode in ("close-pending", "close-return"):
            with self.subTest(mode=mode), self.start(mode) as s:
                self.error(s.request("ui.click", {"window_id": "main", "element_id": "increment"}), "WINDOW_CLOSED")
                self.assertEqual(s.process.wait(timeout=3), 0)
            self.assertFalse(s.forced_cleanup)
            self.assertIn(b"clean", s.stdout)

    def test_missing_completion_times_out_without_success(self):
        with self.start("timeout") as s:
            self.error(s.request("ui.click", {"window_id": "main", "element_id": "increment"}), "TIMEOUT")
            with self.assertRaises(EOFError):
                s.receive()
        self.assertTrue(s.forced_cleanup)

    def test_connection_loss_is_bounded(self):
        with self.start() as s:
            s.sock.close()
        self.assertTrue(s.forced_cleanup)
        self.assertIsNotNone(s.process.returncode)

    def test_late_handler_cannot_ack_after_monotonic_deadline(self):
        with self.start("late-handler") as s:
            self.error(s.request("ui.click", {"window_id": "main", "element_id": "increment"}), "TIMEOUT")
            with self.assertRaises(EOFError):
                s.receive()
        self.assertTrue(s.forced_cleanup)

    def test_duplicate_widget_ids_refuse_startup(self):
        self.startup_refused(mode="duplicate")

    def startup_refused(self, *, mode="", grants="inspect,click,close"):
        parent, child = socket.socketpair()
        parent.settimeout(3)
        env = dict(os.environ, LD_LIBRARY_PATH=str(FIXTURE.parent),
                   TARN_UI_DEV_FD=str(child.fileno()), TARN_UI_DEV_ALLOW=grants,
                   TARN_UI_TEST_MODE=mode, G_DEBUG="fatal-warnings")
        process = subprocess.Popen([str(FIXTURE)], env=env, pass_fds=(child.fileno(),),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        child.close()
        try:
            self.assertEqual(parent.recv(1), b"")
            stdout, stderr = process.communicate(timeout=3)
            self.assertEqual(process.returncode, 1, stderr)
            self.assertIn(b"clean", stdout)
            self.assertEqual(stderr, b"")
        finally:
            parent.close()
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=3)

    def test_empty_or_unknown_grants_refuse_startup(self):
        for grants in ("", "inspect,launch-shell"):
            with self.subTest(grants=grants):
                self.startup_refused(grants=grants)

    def test_production_omits_transport_and_test_hooks(self):
        library = ROOT / ".build/counter/libtarn_ui_gtk.so"
        self.assertNotIn(b"TARN_UI_DEV_FD", library.read_bytes())
        symbols = subprocess.check_output(["nm", "-D", "--defined-only", str(library)])
        self.assertNotIn(b"test_", symbols)
        self.assertNotIn(b"dev_", symbols)
        needed = subprocess.check_output(["readelf", "-d", str(library)])
        self.assertNotIn(b"json-glib", needed)

    def test_automatic_cleanup_does_not_reuse_raw_launcher_ids(self):
        with self.start() as s:
            s.sock.sendall(b'{"version":1,"request_id":"2","method":"ui.inspect","params":{"window_id":"main"}}\n')
            self.assertEqual(s.receive()["request_id"], "2")
        self.assertFalse(s.forced_cleanup)
        self.assertEqual(s.process.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
