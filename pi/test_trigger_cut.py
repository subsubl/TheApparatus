#!/usr/bin/env python3
"""
The Apparatus - Pi B Trigger Watcher - SELF-TEST
================================================
Exercises the mpv IPC logic without any GPIO hardware: pretends the ESP32
just asserted CONTACT and performs the real Layer3 cut against a running
mpv instance (or a mock socket server if --mock is passed).

Usage:
  python3 test_trigger_cut.py [--socket /tmp/apparatus-mpv.sock] [--mock]

With --mock, spins up a fake mpv IPC server that validates the exact command
sequence (loop-a, loop-b, seek, pause) and prints PASS/FAIL.
"""

import json
import os
import socket
import subprocess
import sys
import threading
import time

SOCKET_PATH = "/tmp/apparatus-test-mock.sock"
EXPECTED_SEQUENCE = [
    ("set_property", "ab-loop-a", 300.0),
    ("set_property", "ab-loop-b", 600.0),
    ("seek", 300.0, "absolute", "exact"),
    ("set_property", "pause", False),
]


class MockMpv:
    """Minimal stand-in for mpv's IPC socket that records received commands."""

    def __init__(self, path):
        self.path = path
        self.received = []
        self.fail_next = False
        if os.path.exists(path):
            os.unlink(path)
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(path)
        self.server.listen(1)
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        while True:
            conn, _ = self.server.accept()
            with conn:
                buf = conn.recv(4096).decode(errors="replace")
                for line in buf.splitlines():
                    try:
                        cmd = json.loads(line)
                        self.received.append(cmd["command"])
                        reply = {"error": "failure" if self.fail_next else "success",
                                 "request_id": 0}
                        conn.sendall((json.dumps(reply) + "\n").encode())
                    except (json.JSONDecodeError, KeyError):
                        pass

    def verify(self) -> bool:
        ok = True
        if len(self.received) != len(EXPECTED_SEQUENCE):
            print(f"FAIL: expected {len(EXPECTED_SEQUENCE)} commands, got {len(self.received)}")
            return False
        for i, (got, exp) in enumerate(zip(self.received, EXPECTED_SEQUENCE)):
            # Compare with float tolerance (300 vs 300.0)
            g = [round(x, 4) if isinstance(x, float) else x for x in got]
            e = [round(x, 4) if isinstance(x, float) else x for x in exp]
            if g != e:
                print(f"FAIL: command {i}: expected {e}, got {g}")
                ok = False
        return ok


# Import the module under test (same directory)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trigger_watcher as tw  # noqa: E402


def main() -> int:
    use_mock = "--mock" in sys.argv or not os.environ.get("APPARATUS_MPV_SOCKET")

    if use_mock:
        print("== MOCK MODE: validating command sequence ==")
        mock = MockMpv(SOCKET_PATH)
        tw.MPV_SOCKET = SOCKET_PATH

        time.sleep(0.1)
        result = tw.fire_layer3_cut()
        time.sleep(0.2)

        sequence_ok = mock.verify()
        print(f"cut() returned: {result}")
        print(f"sequence check: {'PASS' if sequence_ok else 'FAIL'}")

        # Negative test: make mpv fail one command
        print("\n== NEGATIVE TEST: mpv rejects a command ==")
        mock.received.clear()
        mock.fail_next = True
        result_neg = tw.fire_layer3_cut()
        print(f"cut() returned on failure: {result_neg} (expected False)")
        neg_ok = result_neg is False

        os.unlink(SOCKET_PATH)

        if sequence_ok and neg_ok and result:
            print("\nALL TESTS PASS")
            return 0
        print("\nTESTS FAILED")
        return 1

    else:
        # Real mpv mode - requires a running mpv with IPC socket
        sock_path = os.environ["APPARATUS_MPV_SOCKET"]
        print(f"== LIVE MODE: firing cut at {sock_path} ==")
        result = tw.fire_layer3_cut()
        print("PASS" if result else "FAIL")
        return 0 if result else 1


if __name__ == "__main__":
    sys.exit(main())