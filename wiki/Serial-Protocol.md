# Serial Protocol

ESP32 (UART1 TX, GPIO2) → Pi B (`/dev/serial0` via PL011), **115200 8N1**,
newline-terminated ASCII. Common GND mandatory.

## Commands

| Command | Sent when | Pi B action (mpv IPC) |
|---------|-----------|----------------------|
| `LOOP_A\n` | IDLE + target beyond far zone | A-B loop → Layer 2 region (0 → 300 s) |
| `LOOP_B\n` | MACRO and viewer inside near zone | A-B loop → Layer 3 region (300 → 600 s), **no seek** |
| `TRIGGER_SEEK\n` | CONTACT rising edge (touch) | ① loop → 300–600 s ② `seek 300 absolute+exact` ③ unpause |

Unknown commands are logged and ignored (forward compatibility).

## Why this order for the cut

Loop points are set **before** the seek: if the seek happened first and mpv
crossed the old loop boundary mid-transition, playback would be yanked back
into Layer 2 — a visible glitch. Loop-first makes it impossible. The seek
target is an exact keyframe position in one contiguous file, so there is no
decoder teardown: zero blackout, zero dropped frames.

Camera keying is deliberately NOT serial: relay 7 presses the mixer's own
camera button (see [[Relay-System]]).

## Daemon (`pi/mpv_daemon.py`)

systemd unit `apparatus-mpv-daemon.service`, bound to the player's lifecycle.
Waits up to 60 s for the IPC socket; retries IPC commands with backoff;
env knobs: `APPARATUS_SERIAL`, `APPARATUS_MPV_SOCKET`,
`APPARATUS_LAYER2_START_S/APPARATUS_LAYER3_START_S/APPARATUS_MASTER_END_S`.

## Self-test

```bash
python3 pi/test_mpv_daemon.py   # mock mpv socket -> ALL TESTS PASS
```
Validates LOOP_A/LOOP_B exact properties, TRIGGER_SEEK ordering, unknown-command
tolerance.
