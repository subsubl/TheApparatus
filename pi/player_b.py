#!/usr/bin/env python3
"""
The Apparatus - Pi B Master Player
==================================
Launches mpv with the 10-minute contiguous master file (Layer2 00:00-05:00,
Layer3 05:00-10:00), IPC socket enabled, and pre-seeds the A-B loop to the
Layer 2 region so playback loops Layer 2 until the ESP32 touch trigger fires.

The trigger_watcher.py daemon owns the IPC socket after this; it re-points the
A-B loop and seeks on CONTACT. This launcher exits once mpv is up - systemd
keeps the Restart=always wrapper around the whole stack via two services.

Key zero-blackout details:
  --ab-loop-a=0 --ab-loop-b=300     start looping Layer 2 only
  --hr-seek=no                      relative seeks stay instant (no keyframe
                                    hunt pause); our trigger uses absolute+exact
                                    which lands precisely on the 05:00 boundary
  --keep-open=no --loop-file=inf    never stop, never show idle screen
"""

import os
import subprocess
import sys
import time

VIDEO = os.environ.get("APPARATUS_MASTER_VIDEO", "/home/pi/media/master_L2_L3.mp4")
MPV_SOCKET = os.environ.get("APPARATUS_MPV_SOCKET", "/tmp/apparatus-mpv.sock")


def wait_for_file(path: str) -> bool:
    start = time.time()
    while not os.path.exists(path):
        if time.time() - start > 120:
            return False
        print(f"[apparatus-B] waiting for {path} ...", flush=True)
        time.sleep(2)
    return True


def main() -> int:
    if not wait_for_file(VIDEO):
        print(f"[apparatus-B] FATAL: {VIDEO} not found", flush=True)
        return 1

    cmd = [
        "mpv",
        f"--input-ipc-server={MPV_SOCKET}",
        # A-B loop seeded to Layer 2 region (00:00 - 05:00)
        "--ab-loop-a=0",
        "--ab-loop-b=300",
        "--keep-open=no",
        "--fullscreen",
        "--no-osd-bar",
        "--osc=no",
        "--hwdec=auto",
        "--profile=high-quality",
        "--video-sync=display-resample",
        "--input-default-bindings=no",
        "--input-vo-keyboard=no",       # no stray keypresses during exhibition
        VIDEO,
    ]
    print("[apparatus-B] launching:", " ".join(cmd), flush=True)
    while True:
        rc = subprocess.call(cmd)
        print(f"[apparatus-B] mpv exited rc={rc}, restarting in 2s", flush=True)
        time.sleep(2)


if __name__ == "__main__":
    sys.exit(main())