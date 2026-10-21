"""Private inherited channel for Phase 3 development sessions; no listener."""
from __future__ import annotations

import json
import os
from pathlib import Path
import socket
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[2]


class Session:
    def __init__(self, *, binary=None, allow=("inspect", "click", "close"),
                 allow_text=False, allow_capture=False, authorize=True, env=None):
        binary = Path(binary or ROOT / ".build/counter-dev/counter").resolve()
        self.sock, child = socket.socketpair()
        self.sock.settimeout(5)
        self.buffer = bytearray()
        self.forced_cleanup = False
        self._shutdown = False
        child_env = os.environ.copy()
        for key in ("TARN_UI_DEV_FD", "TARN_UI_DEV_ALLOW", "TARN_UI_DEV_TEXT", "TARN_UI_DEV_CAPTURE"):
            child_env.pop(key, None)
        child_env.update(env or {})
        child_env.update(LD_LIBRARY_PATH=str(binary.parent), TARN_UI_DEV_FD=str(child.fileno()),
                         TARN_UI_DEV_ALLOW=",".join(allow), TARN_UI_DEV_TEXT="1" if allow_text else "0",
                         TARN_UI_DEV_CAPTURE="1" if allow_capture else "0")
        try:
            self.process = subprocess.Popen([str(binary)], env=child_env,
                                            pass_fds=(child.fileno(),), stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE)
        except BaseException:
            self.sock.close()
            raise
        finally:
            child.close()
        try:
            self.ready = self.receive()
            if self.ready.get("event") != "ready":
                raise RuntimeError("development session did not become ready")
            if authorize:
                reply = self.request("session.authorize", {"session_id": self.ready["session_id"]})
                if "error" in reply:
                    raise RuntimeError("development session refused authorization")
        except BaseException as error:
            self.shutdown()
            error.native_exit = self.process.returncode
            error.native_stdout = self.stdout
            error.native_stderr = self.stderr
            raise

    def receive(self):
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(8192)
            if not chunk:
                raise EOFError("development channel closed")
            self.buffer.extend(chunk)
            if len(self.buffer) > 2 * 1024 * 1024:
                raise ValueError("response budget exceeded")
        line, _, rest = self.buffer.partition(b"\n")
        self.buffer = bytearray(rest)
        return json.loads(line)

    def send(self, method, params, *, version=1):
        # Raw launcher requests may use their own numeric IDs. Automatic bootstrap
        # and cleanup must not collide with those IDs.
        record = {"version": version, "request_id": uuid.uuid4().hex, "method": method, "params": params}
        self.sock.sendall(json.dumps(record, separators=(",", ":")).encode() + b"\n")
        return record["request_id"]

    def request(self, method, params, *, version=1):
        request_id = self.send(method, params, version=version)
        reply = self.receive()
        if reply.get("request_id") != request_id:
            raise ValueError("out-of-order response")
        return reply

    def shutdown(self):
        """Bounded cleanup. Forced cleanup is exposed, never counted as clean exit."""
        if self._shutdown:
            return
        self._shutdown = True
        self.sock.close()
        if self.process.poll() is None:
            self.forced_cleanup = True
            self.process.terminate()
        try:
            self.stdout, self.stderr = self.process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.stdout, self.stderr = self.process.communicate(timeout=3)

    def __enter__(self):
        return self

    def __exit__(self, *exception):
        if self.process.poll() is None:
            try:
                reply = self.request("ui.close", {"window_id": "main"})
                if "result" in reply:
                    self.process.wait(timeout=3)
            except (OSError, EOFError, ValueError, subprocess.TimeoutExpired):
                pass
        self.shutdown()
