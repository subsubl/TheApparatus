#!/usr/bin/env python3
"""
The Apparatus - Pi A Loop Player (autoloader edition)
=====================================================
Layer 1 "Algorithmic Anesthesia" loop -> Mixer CH1.
Videolooper behavior: scans /home/pi/media for layer1_loop*.(mp4|mkv|mov|avi|ts)
(newest wins), gapless loops it, hot-swaps when a newer matching file appears,
shows a gray placeholder card until media is present.

Run under systemd: apparatus-player-a.service
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from media_autoloader import AutoPlayer  # noqa: E402


def main() -> int:
    ap = AutoPlayer(
        stems=("layer1_loop", "layer1"),
        mpv_socket=os.environ.get("APPARATUS_MPV_SOCKET_A", "/tmp/apparatus-mpvA.sock"),
        label="PI-A/L1",
        fixed_args=[
            "--loop-file=inf",           # seamless single-file loop
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