#!/usr/bin/env python3
"""
The Apparatus - Media Autoloader (videolooper-style)
====================================================
Shared engine for BOTH Pis:

  * Scans MEDIA_DIR for correctly-named video files (stem-prefix match,
    newest wins within a stem class).
  * Launches mpv on the best match; gapless looping.
  * HOT RELOAD: polls the directory - if a better/newer matching file
    appears (USB stick copy, rsync, replacement), mpv is gracefully
    restarted on it without touching the other Pi.
  * PLACEHOLDER: if no media found, displays a gray calibration card
    so technicians see "unit alive, awaiting media" instead of a black void.

Filename convention (case-insensitive prefixes):
  Pi A:  layer1_loop*.mp4   (fallback: layer1*)
  Pi B:  master_L2_L3*.mp4  (fallback: master*)

Supported containers: .mp4 .mkv .mov .avi .ts
"""

import os
import shlex
import signal
import subprocess
import sys
import time

VIDEO_EXTS = (".mp4", ".mkv", ".mov", ".avi", ".ts")
POLL_INTERVAL_S = float(os.environ.get("APPARATUS_MEDIA_POLL", "10"))
MEDIA_DIR = os.environ.get("APPARATUS_MEDIA_DIR", "/home/pi/media")
PLACEHOLDER_AFTER_S = float(os.environ.get("APPARATUS_PLACEHOLDER_AFTER", "20"))

TAG = "[autoloader]"


def log(msg):
    print(f"{TAG} {time.strftime('%H:%M:%S')} {msg}", flush=True)


def find_media(media_dir=MEDIA_DIR, stems=("layer1_loop", "layer1")):
    """Return best matching filepath or None.

    Priority: earlier stem in `stems` wins; within same stem, newest
    modification time wins. Case-insensitive prefix match.
    """
    if not os.path.isdir(media_dir):
        return None
    best = None
    best_key = None
    try:
        entries = os.listdir(media_dir)
    except OSError as e:
        log(f"listdir failed: {e}")
        return None
    for fname in entries:
        low = fname.lower()
        if not low.endswith(VIDEO_EXTS):
            continue
        for prio, stem in enumerate(stems):
            if low.startswith(stem):
                path = os.path.join(media_dir, fname)
                try:
                    mtime = os.stat(path).st_mtime_ns
                except OSError:
                    break
                key = (prio, -mtime)
                if best_key is None or key < best_key:
                    best_key = key
                    best = path
                break
    return best


class AutoPlayer:
    """Manages one mpv process with directory-watch hot swapping."""

    def __init__(self, stems, mpv_socket, fixed_args, label):
        self.stems = tuple(stems)
        self.socket = mpv_socket
        self.fixed_args = fixed_args          # e.g. ["--vo=gpu","--gpu-context=drm"]
        self.label = label
        self.proc = None
        self.current_path = None
        self.current_mtime = None
        self.started_monotonic = time.monotonic()
        self._stop = False

    # ---- process management -------------------------------------------------

    def _spawn(self, path):
        cmd = ["mpv", f"--input-ipc-server={self.socket}", *self.fixed_args]
        if path:
            cmd.append(path)
        else:
            # Gray placeholder: instantly visible "no media" card
            cmd += [
                "--loops=inf",
                "--no-audio",
                "--force-media-title",
                f"{self.label}: NO MEDIA in {MEDIA_DIR}",
                "av://lavfi:color=c=0x202020:s=1920x1080:r=25",
            ]
        log(f"{self.label} launching: {shlex.join(cmd)}")
        self.proc = subprocess.Popen(cmd)

    def _shutdown_current(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=4)
            except OSError:
                pass
        self.proc = None
        self.current_path = None
        self.current_mtime = None

    # ---- main loop ------------------------------------------------------------

    def run_forever(self):
        signal.signal(signal.SIGTERM, self._sigterm)

        while not self._stop:
            target = find_media(stems=self.stems)
            t_mtime = None
            if target:
                t_mtime = os.stat(target).st_mtime_ns

            playing_placeholder = self.proc is not None and self.current_path is None

            need_swap = (
                self.proc is None
                or (target is None and not playing_placeholder)
                or (target is not None and target != self.current_path)
                or (target is not None and t_mtime != self.current_mtime)
            )

            # Placeholder grace: give the media a moment before showing gray
            if self.proc is None and target is None:
                if (time.monotonic() - self.started_monotonic) < PLACEHOLDER_AFTER_S:
                    time.sleep(POLL_INTERVAL_S)
                    continue

            if need_swap:
                if self.current_path != target:
                    log(f"{self.label} switch -> {target or 'PLACEHOLDER'}")
                self._shutdown_current()
                self._spawn(target)
                self.current_path = target
                self.current_mtime = t_mtime
                self.started_monotonic = time.monotonic()

            time.sleep(POLL_INTERVAL_S)

    def _sigterm(self, signum, frame):
        log(f"{self.label} SIGTERM - shutting down mpv")
        self._stop = True
        self._shutdown_current()
        sys.exit(0)


def main():  # pragma: no cover - CLI probe helper
    stems = tuple(sys.argv[1:]) or ("layer1_loop", "layer1")
    hit = find_media(stems=stems)
    print(hit or "NO MATCH")
    return 0


if __name__ == "__main__":
    sys.exit(main())