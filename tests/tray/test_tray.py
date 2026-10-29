"""Tray integration end to end on an isolated bus (run under dbus-run-session):
fixtures play the tray host and the notification server, the development
channel drives and inspects the window."""
import os
import subprocess
import sys
import time
import unittest
from pathlib import Path

from gi.repository import Gio, GLib

ROOT = Path(__file__).resolve().parents[2]
DBUS_TESTS = Path(os.environ.get("TARN_DBUS", ROOT.parent / "libs/dbus")) / "tests"
sys.path.insert(0, str(ROOT / "tests/devtools"))
from reference_session import Session

BINARY = ROOT / ".build/tray-dev/tray"
V = GLib.Variant
ITEM, MENU = "org.kde.StatusNotifierItem", "com.canonical.dbusmenu"


def fixture(name):
    process = subprocess.Popen([sys.executable, str(DBUS_TESTS / name)], stdout=subprocess.PIPE, text=True)
    assert process.stdout.readline().strip() == "READY"
    return process


def stop(process):
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=5)
    process.stdout.close()


def cpu_ticks(pid):
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return int(fields[11]) + int(fields[12])


class TrayTests(unittest.TestCase):
    def setUp(self):
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION)
        self.notifications = fixture("notifications_fixture.py")
        self.addCleanup(stop, self.notifications)

    def start(self):
        config = ROOT / ".build/audit-config"
        return Session(binary=BINARY, allow=("inspect", "click", "close"), allow_text=True,
                       env={"GTK_THEME": "Adwaita", "XDG_CONFIG_HOME": str(config), "GDK_DEBUG": "no-portals"})

    def call(self, name, path, interface, member, params=None):
        return self.bus.call_sync(name, path, interface, member, params, None, Gio.DBusCallFlags.NONE,
                                  3000, None).unpack()

    def visible(self, session):
        reply = session.request("ui.inspect", {"window_id": "main"})
        # NOT_READY: presented but not mapped yet.
        if reply.get("error", {}).get("code") == "NOT_READY":
            return False
        self.assertIn("result", reply, reply)
        return reply["result"]["root"]["visible"]

    def wait_for(self, session, condition, what):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if condition():
                return
            time.sleep(0.05)
        self.fail(what)

    def last_summary(self):
        last = self.call("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                         "org.tarn.NotifyTest", "Last")[0]
        return last[3] if last else None

    def tooltip(self, service):
        return self.call(service, "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "Get",
                         V("(ss)", (ITEM, "ToolTip")))[0][3]

    def test_window_lives_in_the_tray(self):
        watcher = fixture("tray_watcher.py")
        self.addCleanup(stop, watcher)
        with self.start() as session:
            service = watcher.stdout.readline().split()[1]
            self.assertTrue(self.visible(session))

            # Closing hides the window and tells the user once where it went.
            reply = session.request("ui.close", {"window_id": "main"})
            self.assertTrue(reply["result"]["hidden"])
            self.assertFalse(self.visible(session))
            self.assertIsNone(session.process.poll())
            self.wait_for(session, lambda: self.last_summary() == "Sigo en la bandeja", "hidden notice")

            # A click on the icon toggles the window.
            self.call(service, "/StatusNotifierItem", ITEM, "Activate", V("(ii)", (0, 0)))
            self.wait_for(session, lambda: self.visible(session), "Activate shows the window")
            self.call(service, "/StatusNotifierItem", ITEM, "Activate", V("(ii)", (0, 0)))
            self.wait_for(session, lambda: not self.visible(session), "Activate hides it again")
            self.call(service, "/MenuBar", MENU, "Event", V("(isvu)", (1, "clicked", V("i", 0), 0)))
            self.wait_for(session, lambda: self.visible(session), "Mostrar shows the window")

            # A button in the window sends a notification; its action reopens the window.
            session.request("ui.click", {"window_id": "main", "element_id": "notify"})
            self.wait_for(session, lambda: self.last_summary() == "Hola desde Tarn UI", "notification sent")
            self.wait_for(session, lambda: self.tooltip(service) == "Avisos enviados: 2", "tray tooltip follows the model")
            session.request("ui.click", {"window_id": "main", "element_id": "hide"})
            self.wait_for(session, lambda: not self.visible(session), "Ocultar hides")
            self.call("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.tarn.NotifyTest",
                      "Invoke", V("(us)", (2, "open")))
            self.wait_for(session, lambda: self.visible(session), "notification action shows the window")

            # The menu checkbox is app state; the tray reflects it.
            self.call(service, "/MenuBar", MENU, "Event", V("(isvu)", (2, "clicked", V("i", 0), 0)))
            self.wait_for(session, lambda: self.tooltip(service).endswith("no molestar"), "toggle reaches the model")
            state = self.call(service, "/MenuBar", MENU, "GetProperty", V("(is)", (2, "toggle-state")))[0]
            self.assertEqual(state, 1)

            # Idle: the socket watch must not spin.
            before = cpu_ticks(session.process.pid)
            time.sleep(2)
            self.assertLessEqual(cpu_ticks(session.process.pid) - before, 2)

            # Salir ends the app and unpublishes the item.
            self.call(service, "/MenuBar", MENU, "Event", V("(isvu)", (4, "clicked", V("i", 0), 0)))
            self.assertEqual(session.process.wait(timeout=5), 0)
            names = self.call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames")[0]
            self.assertNotIn(service, names)

    def test_without_a_tray_host_closing_ends_the_app(self):
        with self.start() as session:
            self.assertTrue(self.visible(session))
            self.assertIn("result", session.request("ui.close", {"window_id": "main"}))
            self.assertEqual(session.process.wait(timeout=5), 0)


if __name__ == "__main__":
    unittest.main()
