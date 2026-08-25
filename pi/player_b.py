#!/usr/bin/env python3
"""
The Apparatus - Pi B Master Player (autoloader edition)
=======================================================
Master L2/L3 file -> Mixer CH2. Videolooper behavior identical to Pi A but
matching master_L2_L3*.(mp4|mkv|mov|avi|ts).

A-B loop is seeded to the Layer 2 region (00:00-05:00) via mpv launch flags,
so EVERY respawn (including autoloader hot-swaps) re-seeds Layer 2 looping.
The serial daemon (mpv_daemon.py) owns the IPC socket afterwards and re-points
loop + seeks on CONTACT.

Run under systemd: apparatus-player-b.service
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from media_autoloader import AutoPlayer  # noqa: E402


def main() -> int:
    layer3_start = os.environ.get("APPARATUS_LAYER3_START_S", "300")
    ap = AutoPlayer(
        stems=("master_l2_l3", "master"),
        mpv_socket=os.environ.get("APPARATUS_MPV_SOCKET", "/tmp/apparatus-mpv.sock"),
        label="PI-B/MASTER",
        fixed_args=[
            f"--ab-loop-a=0",             # Layer 2 region start
            f"--ab-loop-b={layer3_start}",  # Layer 3 boundary
            "--keep-open=no",
            "--fullscreen",
            "--no-osd-bar",
            "--osc=no",
            "--hwdec=auto",
            "--profile=high-quality",
            "--video-sync=display-resample",
            "--vo=gpu",
            "--gpu-context=drm",
            "--input-default-bindings=no",
            "--input-vo-keyboard=no",
        ],
    )
    ap.run_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())