#!/usr/bin/env python3
"""
The Apparatus - Pi B Trigger Watcher
====================================
Watches the ESP32 PI_TRIGGER_PIN (GPIO 19) and drives mpv's IPC socket to
execute the zero-blackout Layer2 -> Layer3 cut:

  1. Shift A-B loop points to 05:00 - 10:00 (Layer 3 region)
  2. Seek instantly to 05:00

Because Layers 2 & 3 live in ONE contiguous H.264 file and the seek lands on a
keyframe boundary at exactly 05:00, mpv performs an instant internal switch -
no decoder teardown, no blackout, no dropped frames.

Rising-edge only (ESP32 holds the line HIGH for the whole CONTACT state),
with a cooldown to reject contact-bounce / repeated touches within one scene.

Requires: python3-gpiod (apt install python3-gpiod)
Tested on: Raspberry Pi OS Bookworm (gpiod 1.6+)
"""

import json
import os
import socket
import sys
import time

# ----------------------------------------------------------------------------
# Configuration (override via environment if needed)
# ----------------------------------------------------------------------------
GPIO_CHIP      = os.environ.get("APPARATUS_GPIO_CHIP", "/dev/gpiochip0")
TRIGGER_GPIO   = int(os.environ.get("APPARATUS_TRIGGER_GPIO", "19"))
                # BCM19 == physical pin 35 on the Pi header. Wire ESP32
                # PI_TRIGGER_PIN here AND tie Pi GND to ESP32 GND!
MPV_SOCKET     = os.environ.get("APPARATUS_MPV_SOCKET", "/tmp/apparatus-mpv.sock")
LAYER3_START_S = float(os.environ.get("APPARATUS_LAYER3_START", "300"))  # 05:00
MASTER_END_S   = float(os.environ.get("APPARATUS_MASTER_END", "600"))    # 10:00
COOLDOWN_S     = float(os.environ.get("APPARATUS_COOLDOWN", "5.0"))
POLL_FALLBACK  = True   # use polling read if edge event API unavailable

LOG_PREFIX = "[apparatus]"


def log(msg: str):
    print(f"{LOG_PREFIX} {time.strftime('%H:%M:%S')} {msg}", flush=True)


def mpv_command(sock_path: str, cmd: dict, retries: int = 3) -> bool:
    """Send a single JSON command over mpv's IPC unix socket."""
    payload = (json.dumps(cmd) + "\n").encode()
    for attempt in range(retries):
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
                s.settimeout(2.0)
                s.connect(sock_path)
                s.sendall(payload)
                resp = s.recv(4096).decode(errors="replace")
                # mpv replies {"error":"success"} on success
                try:
                    rj = json.loads(resp.splitlines()[0])
                    if rj.get("error") == "success":
                        return True
                    log(f"mpv error response: {rj}")
                except (json.JSONDecodeError, IndexError):
                    log(f"mpv unparseable response: {resp!r}")
        except (ConnectionRefusedError, FileNotFoundError):
            log(f"cannot reach mpv socket {sock_path} (attempt {attempt + 1}/{retries})")
        except OSError as e:
            log(f"socket error: {e} (attempt {attempt + 1}/{retries})")
        time.sleep(0.5 * (attempt + 1))
    return False


def fire_layer3_cut() -> bool:
    """Re-point the A-B loop into the Layer 3 region and seek there instantly.

    Order matters: set loop points FIRST, then seek. If the viewer is mid-
    Layer2-loop when we cut, mpv's A-B loop would otherwise yank playback back;
    setting the new boundaries before seeking guarantees the post-seek position
    sits inside the active A-B window.
    """
    ok_loop_a = mpv_command(MPV_SOCKET, {
        "command": ["set_property", "ab-loop-a", LAYER3_START_S]
    })
    ok_loop_b = mpv_command(MPV_SOCKET, {
        "command": ["set_property", "ab-loop-b", MASTER_END_S]
    })
    ok_seek = mpv_command(MPV_SOCKET, {
        "command": ["seek", LAYER3_START_S, "absolute", "exact"]
    })
    ok_pause = mpv_command(MPV_SOCKET, {
        "command": ["set_property", "pause", False]
    })

    success = all([ok_loop_a, ok_loop_b, ok_seek])
    log(f"LAYER3 CUT {'OK' if success else 'FAILED'} "
        f"(loop_a={ok_loop_a} loop_b={ok_loop_b} seek={ok_seek} resume={ok_pause})")
    return success


def main() -> int:
    log(f"starting: gpio={TRIGGER_GPIO} chip={GPIO_CHIP} sock={MPV_SOCKET}")

    # Wait for mpv to come up (socket file appears)
    deadline = time.time() + 60
    while not os.path.exists(MPV_SOCKET):
        if time.time() > deadline:
            log("FATAL: mpv IPC socket never appeared - is mpv running?")
            return 1
        time.sleep(1)
    log("mpv IPC socket detected")

    import gpiod

    chip = gpiod.Chip(GPIO_CHIP)
    line = chip.get_line(TRIGGER_GPIO)
    # The ESP32 drives this line push-pull; we consume it as active-high input.
    line.request(consumer="apparatus-trigger",
                 type=gpiod.LINE_REQ_DIR_IN)

    last_level = line.get_value()
    last_trigger_time = 0.0
    log(f"watching GPIO{TRIGGER_GPIO}, initial level={last_level}")

    while True:
        level = line.get_value()

        # Rising edge = CONTACT engaged on ESP32
        if level == 1 and last_level == 0:
            now = time.monotonic()
            if now - last_trigger_time >= COOLDOWN_S:
                log("rising EDGE detected -> firing Layer 3 cut")
                fire_layer3_cut()
                last_trigger_time = now
            else:
                log(f"edge ignored (cooldown {COOLDOWN_S:.0f}s)")
        last_level = level

        time.sleep(0.01)   # 100 Hz poll; latency budget is trivially met


if __name__ == "__main__":
    sys.exit(main())