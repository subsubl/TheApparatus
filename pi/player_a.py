#!/usr/bin/env python3
"""
The Apparatus - Pi A Loop Player
================================
Plays the pristine Layer 1 loop on Pi A (Mixer CH1) with gapless repeat.

mpv's --loop-file=yes on a single file is effectively gapless for H.264 when
the file ends on a closed GOP; --gapless-audio is irrelevant here (silent or
ambient track), and --keep-open=no ensures immediate restart.

Run under systemd: apparatus-player-a.service
"""

import os
import subprocess
import sys
import time

VIDEO = os.environ.get("APPARATUS_LAYER1_VIDEO", "/home/pi/media/layer1_loop.mp4")
MPV_SOCKET = "/tmp/apparatus-mpvA.sock"


def wait_for_file(path: str, timeout_s: float = 0) -> bool:
    """Wait indefinitely (or timeout) for the media file to exist."""
    start = time.time()
    while not os.path.exists(path):
        if timeout_s and (time.time() - start) > timeout_s:
            return False
        print(f"[apparatus-A] waiting for {path} ...", flush=True)
        time.sleep(2)
    return True


def main() -> int:
    if not wait_for_file(VIDEO):
        print(f"[apparatus-A] FATAL: {VIDEO} not found", flush=True)
        return 1

    cmd = [
        "mpv",
        f"--input-ipc-server={MPV_SOCKET}",
        "--loop-file=inf",              # seamless single-file loop
        "--keep-open=no",
        "--fullscreen",
        "--no-osd-bar",
        "--osc=no",
        "--hwdec=auto",                 # V4L2 request API / mmal where available
        "--profile=high-quality",
        "--video-sync=display-resample",
        VIDEO,
    ]
    print("[apparatus-A] launching:", " ".join(cmd), flush=True)
    while True:
        rc = subprocess.call(cmd)
        print(f"[apparatus-A] mpv exited rc={rc}, restarting in 2s", flush=True)
        time.sleep(2)


if __name__ == "__main__":
    sys.exit(main())