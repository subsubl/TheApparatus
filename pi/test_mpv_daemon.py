#!/usr/bin/env python3
"""
The Apparatus - Pi B mpv Daemon - SELF-TEST (mock IPC, no hardware)
===================================================================
Validates the full serial command protocol against a mock mpv socket:
  LOOP_A       -> loop 0-300
  LOOP_B       -> loop 300-600
  TRIGGER_SEEK -> loop 300-600 + seek 300 exact + resume

Usage: python3 test_mpv_daemon.py   ->  ALL TESTS PASS / TESTS FAILED
"""

import importlib.util
import json
import os
import socket
import sys
import threading
import time

MOCK_SOCK = "/tmp/apparatus-test-mock.sock"


class MockMpv:
    def __init__(self, path):
        self.path = path
        self.received = []
        if os.path.exists(path):
            os.unlink(path)
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(path)
        self.server.listen(1)
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self):
        while True:
            conn, _ = self.server.accept()
            with conn:
                buf = conn.recv(4096).decode(errors="replace")
                for line in buf.splitlines():
                    try:
                        cmd = json.loads(line)["command"]
                        self.received.append(cmd)
                        conn.sendall((json.dumps({"error": "success"}) + "\n").encode())
                    except Exception:
                        pass


spec = importlib.util.spec_from_file_location(
    "mpv_daemon", os.path.join(os.path.dirname(os.path.abspath(__file__)), "mpv_daemon.py"))
md = importlib.util.module_from_spec(spec)
spec.loader.exec_module(md)


def cmds_equal(got, expected):
    if len(got) != len(expected):
        return False
    for g, e in zip(got, expected):
        if len(g) != len(e) or g[0] != e[0] or g[1] != e[1]:
            return False
        for gv, ev in zip(g[2:], e[2:]):
            try:
                if round(float(gv), 3) != float(ev):
                    return False
            except (ValueError, TypeError):
                if gv != ev:
                    return False
    return True


def main():
    mock = MockMpv(MOCK_SOCK)
    md.MPV_SOCKET = MOCK_SOCK
    time.sleep(0.1)

    failures = []

    # --- LOOP_A ---
    mock.received.clear()
    md.handle("LOOP_A")
    time.sleep(0.15)
    exp = [["set_property", "ab-loop-a", 0.0], ["set_property", "ab-loop-b", 300.0]]
    if not cmds_equal(mock.received, exp):
        failures.append(f"LOOP_A: got {mock.received}")

    # --- LOOP_B ---
    mock.received.clear()
    md.handle("LOOP_B")
    time.sleep(0.15)
    exp = [["set_property", "ab-loop-a", 300.0], ["set_property", "ab-loop-b", 600.0]]
    if not cmds_equal(mock.received, exp):
        failures.append(f"LOOP_B: got {mock.received}")

    # --- TRIGGER_SEEK: loop first, then seek, then resume ---
    mock.received.clear()
    cut_ok = md.handle("TRIGGER_SEEK")
    time.sleep(0.15)
    exp = [["set_property", "ab-loop-a", 300.0],
           ["set_property", "ab-loop-b", 600.0],
           ["seek", 300.0, "absolute", "exact"],
           ["set_property", "pause", False]]
    if not cmds_equal(mock.received, exp):
        failures.append(f"TRIGGER_SEEK: got {mock.received}")
    if not cut_ok:
        failures.append("TRIGGER_SEEK returned False on success path")

    # --- unknown command ignored ---
    mock.received.clear()
    md.handle("GARBAGE")
    time.sleep(0.1)
    if mock.received:
        failures.append(f"GARBAGE produced commands: {mock.received}")

    os.unlink(MOCK_SOCK)

    if failures:
        for f in failures:
            print(f"FAIL: {f}")
        print("TESTS FAILED")
        return 1
    print("ALL TESTS PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())