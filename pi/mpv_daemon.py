#!/usr/bin/env python3
"""
The Apparatus - Pi B mpv Daemon (serial protocol edition)
=========================================================
Reads newline-terminated ASCII commands from the ESP32 over serial and drives
mpv's IPC socket:

  LOOP_A        -> seed A-B loop to Layer 2 region (00:00 - 05:00)
  LOOP_B        -> hint loop into Layer 3 region (05:00 - 10:00)
  TRIGGER_SEEK  -> CONTACT hard cut: re-point A-B to Layer 3 + instant
                   exact seek to 05:00 (zero-blackout)
  CAMERA_ON     -> live camera feed active (face superimposition layer)
  CAMERA_OFF    -> live camera feed off

Wiring: ESP32 PI_LINK_TX (GPIO2) -> Pi RXD (GPIO15 on Pi header, /dev/serial0)
Common GND mandatory. 115200 8N1.

Run under systemd: apparatus-trigger-watcher.service (renamed role:
it is now BOTH the serial listener and the cut executor).
"""

import json
import os
import socket
import subprocess
import sys
import time

MPV_SOCKET     = os.environ.get("APPARATUS_MPV_SOCKET", "/tmp/apparatus-mpv.sock")
SERIAL_PORT    = os.environ.get("APPARATUS_SERIAL", "/dev/serial0")
BAUD           = int(os.environ.get("APPARATUS_BAUD", "115200"))
LAYER2_START_S = float(os.environ.get("APPARATUS_LAYER2_START", "0"))
LAYER3_START_S = float(os.environ.get("APPARATUS_LAYER3_START", "300"))
MASTER_END_S   = float(os.environ.get("APPARATUS_MASTER_END", "600"))

LOG = "[apparatus-pi]"


def log(msg):
    print(f"{LOG} {time.strftime('%H:%M:%S')} {msg}", flush=True)


def mpv_command(cmd, retries=3):
    payload = (json.dumps({"command": cmd}) + "\n").encode()
    for attempt in range(retries):
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
                s.settimeout(2.0)
                s.connect(MPV_SOCKET)
                s.sendall(payload)
                resp = s.recv(4096).decode(errors="replace")
                rj = json.loads(resp.splitlines()[0])
                if rj.get("error") == "success":
                    return True
                log(f"mpv error: {rj}")
        except FileNotFoundError:
            log(f"mpv socket missing: {MPV_SOCKET}")
        except (OSError, json.JSONDecodeError, IndexError) as e:
            log(f"ipc err: {e}")
        time.sleep(0.4 * (attempt + 1))
    return False


def set_loop(a, b):
    ok_a = mpv_command(["set_property", "ab-loop-a", a])
    ok_b = mpv_command(["set_property", "ab-loop-b", b])
    return ok_a and ok_b


def handle(cmd) -> bool:
    """Returns True when the command produced the Layer-3 cut."""
    cmd = cmd.strip().upper()

    if cmd == "LOOP_A":
        ok = set_loop(LAYER2_START_S, LAYER3_START_S)
        log(f"LOOP_A {'ok' if ok else 'FAILED'} (loop {LAYER2_START_S}-{LAYER3_START_S}s)")
        return False

    elif cmd == "LOOP_B":
        # Soft hint: move loop start toward Layer 3 but do not seek.
        # Playback naturally crosses the boundary or stays put - no visual jump.
        ok = set_loop(LAYER3_START_S, MASTER_END_S)
        log(f"LOOP_B {'ok' if ok else 'FAILED'} (loop {LAYER3_START_S}-{MASTER_END_S}s)")
        return False

    elif cmd == "TRIGGER_SEEK":
        # ORDER MATTERS: new loop points first, then exact seek, then resume.
        ok_loop = set_loop(LAYER3_START_S, MASTER_END_S)
        ok_seek = mpv_command(["seek", LAYER3_START_S, "absolute", "exact"])
        mpv_command(["set_property", "pause", False])
        ok = ok_loop and ok_seek
        log(f"TRIGGER_SEEK {'OK - LAYER 3 LIVE' if ok else 'FAILED'}")
        return ok

    elif cmd == "CAMERA_ON":
        # Live-camera activation for the Layer 3 face-superimposition.
        # Hook: launch/notify the compositing pipeline here. Kept as a
        # subprocess hook so the visual system can evolve independently
        # of this daemon (e.g. OpenCV compositor, OBS, or a second mpv).
        script = os.environ.get("APPARATUS_CAMERA_SCRIPT",
                                "/home/pi/apparatus/camera_compositor.py")
        if os.path.exists(script):
            subprocess.Popen(["python3", script, "on"])
            log("CAMERA_ON -> compositor activated")
        else:
            log(f"CAMERA_ON received (no compositor at {script} - stub)")
        return False

    elif cmd == "CAMERA_OFF":
        script = os.environ.get("APPARATUS_CAMERA_SCRIPT",
                                "/home/pi/apparatus/camera_compositor.py")
        if os.path.exists(script):
            subprocess.Popen(["python3", script, "off"])
            log("CAMERA_OFF -> compositor deactivated")
        else:
            log("CAMERA_OFF received (no compositor - stub)")
        return False

    elif cmd:
        log(f"unknown cmd: {cmd!r}")
    return False


def main():
    log(f"starting: serial={SERIAL_PORT}@{BAUD} sock={MPV_SOCKET}")

    deadline = time.time() + 60
    while not os.path.exists(MPV_SOCKET):
        if time.time() > deadline:
            log("FATAL: mpv IPC socket never appeared")
            return 1
        time.sleep(1)
    log("mpv detected")

    import serial  # pyserial: apt install python3-serial

    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=1)
    buf = b""

    while True:
        chunk = ser.read(64)
        if chunk:
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                try:
                    handle(line.decode(errors="replace"))
                except Exception as e:
                    log(f"handler exception: {e}")
        else:
            time.sleep(0.01)


if __name__ == "__main__":
    sys.exit(main())