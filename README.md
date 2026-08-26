# The Apparatus — ESP32 Firmware (WJ-AVE5 Edition)

Firmware for "The Apparatus" — an interactive multimedia art installation.
An ESP32-WROOM-32 reads an HLK-LD2410 24 GHz mmWave radar, extracts respiration
via a DSP pipeline, drives **6 vactrols** bridging the sliders of a
circuit-bent **Panasonic WJ-AVE5** analog video mixer, presses its effect
buttons through **8 relays**, and exposes a full calibration/control WebUI.

## Hardware Architecture

| Component | Connection | Details |
|-----------|------------|---------|
| **MCU** | ESP32-WROOM-32 | 240 MHz dual-core, standalone Wi-Fi AP |
| **Radar** | HLK-LD2410 | UART2 (GPIO16 RX / GPIO17 TX), 256 kbps, Engineering Mode via cmd **0x62** |
| **Vactrols ×6** | WJ-AVE5 sliders | LEDC @ 10-bit / 5 kHz: Mix/T-Bar GPIO13, Color X GPIO14, Color Y GPIO25, Wipe Speed GPIO26, Effect Level GPIO27, Aux Mod GPIO33 |
| **Relays ×8** | WJ-AVE5 buttons (active-LOW board) | GPIO4, 18, 19, 21, 22, 23, 32, 15 — **GUI-remappable at runtime** |
| **Performer buttons ×8** | Physical buttons | GPIO5, 12, then input-only bank 34, 35, 36, 39 + spares (external pull-downs required) |
| **Touch plate** | Bare metal plate → **ESP32 internal touch peripheral, T5 = GPIO12** (1 kΩ series + 10 kΩ pull-down; no external touch IC) |
| **Pi B link** | UART1 TX → Pi serial0 | GPIO2 @ 115200 8N1 — ASCII protocol `LOOP_A` / `LOOP_B` / `TRIGGER_SEEK` |
| **Status LED** | Onboard | GPIO2* (see PinDefinitions.h; move if conflicting with PiLink TX) |

> ⚠️ GPIO 2 doubles as PiLink TX. The status heartbeat defaults there only when
> the pin is free — check `PinDefinitions.h` before wiring both.

## Signal Flow

```
HLK-LD2410 ─UART2─▶ RadarParser ─▶ EMA(500ms) ─▶ PeakGate+Centroid ─▶ Biquad(0.1–0.5Hz) ─▶ AGC[-1,+1]
                                                                                        │
Touch(GPIO12/T5)──┐                                                                  │
                     ▼                                                                  ▼
              ╔══════════════════════ 4-TIER STATE MACHINE ════════════════════╗
              ║ IDLE(0%) ⇄ MACRO(dist^γ + slew) ⇄ MICRO(P_base + breath·M)    ║
              ║                    CONTACT = hard override (100% + L3 cut)     ║
              ╚═══╦═══════════════╦══════════════╦════════════════╦════════════╝
                  ▼               ▼              ▼                ▼
          6× Vactrol PWM   8× Relay seq    BootSequencer    UART1 "TRIGGER_SEEK"
          (mixer sliders)  (+clock mode)   (power-on ritual)     → Pi B mpv IPC
```

## Relay Subsystem (WJ-AVE5 Button Spoofing)

Each relay is wired **across** a mixer button. Closing the relay = pressing it.

### Per-relay configuration (all GUI-editable, NVS-persisted)
- **Trigger source**: Manual · Layer 1 Return (Idle) · Layer 2 Entry (Macro) ·
  Breath Lock (Micro) · Layer 3 Cut (Contact) · Inhale Peak · Exhale Peak
- **Press shape**: press length (30–3000 ms), press count 1–5 (**double/triple-click**),
  gap between presses
- **Clock mode**: re-fire the sequence automatically every N ms
- **GPIO remap**: any relay can be moved to another pin from the WebUI dropdown
  (conflicts and protected pins are rejected live)

### Performer buttons (physical)
- 1 click → configured sequence
- 2/3 clicks → one extended press (×2/×3 length, capped 2 s)
- Long-press (> configurable threshold) → momentary hold

### Boot Sequence (Power-On Ritual)
When the mixer is plugged into mains and the ESP32 boots, a **configurable,
non-blocking sequencer** performs the wake-up button choreography:
- Start delay (default 3 s) to let the mixer PSU stabilize
- Up to 12 steps, each: relay slot, N presses (double/triple supported),
  press length, inter-press gap, dwell after step
- Default step 1 = long power-button press on BTN1, step 2 = arm effect on BTN2
- Editable and replayable ("REPLAY NOW") from the WebUI, persisted in NVS
- State memory: all settings survive power cycles via NVS (`Preferences.h`)

## DSP Pipeline Details

### 1. EMA Filter (500 ms time constant)
- Sample rate: 10 Hz (100 ms period)
- α = 1 - e^(-dt/τ) = 1 - e^(-0.1/0.5) ≈ 0.181
- Eliminates clothing noise, spike artifacts

### 2. Peak-Gate Selection + Sub-Gate Centroid Interpolation
- Gate size: 75 cm (9 gates = 0–675 cm), gate energies are **single-byte** values
- **Peak stationary-energy gate (G_target)** selected per frame
- Fractional position: α = (Distance mod 75) / 75
- Virtual energy blends G_peak with its sway-direction neighbor:
  E_virtual = (1−w)·E_peak + w·E_neighbor
- Prevents step artifacts when the viewer sways across gate boundaries

### 3. Biquad Bandpass Filter (IIR)
- Passband: 0.1 Hz – 0.5 Hz (respiration range), 10 Hz sample rate
- Direct Form I implementation

### 4. Sliding-Window AGC
- Window: 40 samples (4 s); tracks peak envelope E_max
- N_resp = y[n] / (E_max + ε), strictly bounded [-1.0, +1.0]

## LD2410 Protocol Notes (verified vs. datasheet v1.02/v1.07)

- **Engineering mode enable = `0x62`** (disable `0x63`). `0x61` is *read-parameters* — a common trap.
- Config sequence: `0xFF (value 01 00)` → `0x62` → `0xFE`. Flag is volatile; re-applied every boot.
- Engineering report payload (`type=0x01`, wire length `0x23`):
  `AA | state(1) | mov_dist(2) | mov_E(1) | still_dist(2) | still_E(1) | det_dist(2) | max_mov_gate(1) | max_still_gate(1) | moving_gates(9×1) | stationary_gates(9×1) | 55 00`
- ACK frames matched by echoed command word + status byte — no timing heuristics.

## State Machine

| State | Condition | Behavior |
|-------|-----------|----------|
| **IDLE (0)** | No target or distance > D_max + hysteresis | Mix vactrol → 0%, Layer 1 regime |
| **MACRO (1)** | Target in [D_min, D_max], moving | Distance → PWM with gamma shaping + slew limit |
| **MICRO (2)** | Variance < thr for > lock time | Lock P_base, add breathing: P_out = P_base + N_resp × M |
| **CONTACT (3)** | Touch sensed on GPIO12 (T5) | Mix = 100% instantly, serial `TRIGGER_SEEK` to Pi B |

Bidirectional hysteresis at D_max prevents boundary chatter.

## WebUI (http://192.168.4.1 — AP `TheApparatus_AP`)

Panels:
1. **System state** — badge, mix PWM, Pi trigger, distances, gamma
2. **Radar gates** — 9 energy bars, peak gate highlighted ◄
3. **Radar Preview** — top-down view of the room: LD2410 at bottom, gate arcs
   every 75 cm, D_min/D_max band, Pi zone lines, live target dot (color-coded by
   state, ♥ pulse in MICRO breathing lock)
4. **Breathing oscilloscope** — biquad raw + AGC normalized traces @ 20 Hz
5. **Vactrol channels ×6** — AUTO/MANUAL toggle, manual slider, min/max clamp, per-channel slew
6. **Sensor calibration** — D_min/D_max/hysteresis/gamma/M/slew/variance/breath-threshold/Pi zones + PWM clamps → Save to NVS / Factory reset
7. **Relay bank ×8** — per relay: **AVE5 target-button dropdown** (STILL,
   STROBE, MOSAIC, PAINT, … SUPERIMPOSE), trigger dropdown, press length/count/
   gap, clock toggle + interval, **GPIO remap dropdown**, FIRE/STOP, live dot.
   The whole bank persists to NVS on Save.
8. **Boot sequence editor** — 12 steps, per-step relay/N-presses/length/gap/dwell,
   start delay, replay button.

WebSocket messages: `{type:"telemetry"|"config"|"saved"|...}`, client→server
commands: `get_config`, `relay_fire`, `relay_stop`, `relay_cfg`, `relay_pin`,
`vactrol_auto`, `vactrol_manual`, `save_config`, `boot_replay`, `factory_reset`.

## Building & Flashing

```bash
pio run                      # production firmware (real radar)
pio run -e esp32dev_sim      # SIM MODE - synthetic scenario, no radar needed
pio run --target upload && pio device monitor --baud 115200
```

SIM scenario (60 s loop @ 10 Hz): approach → MACRO, stationary ±2 cm with
0.25 Hz breathing → MICRO, +180 cm outlier absorbed by EMA at t=30 s, retreat →
IDLE.

## Project Structure

```
TheApparatus/
├── platformio.ini              # envs: esp32dev (prod), esp32dev_sim (SIM_MODE)
├── include/
│   ├── PinDefinitions.h        # Pins, enums, CalibrationConfig (NVS blob), BootSequencer
│   ├── RadarParser.h           # LD2410 Engineering Mode driver
│   ├── DSP.h                   # EMA, Gate Interpolation, Biquad, AGC
│   ├── StateMachine.h          # 4-tier state machine
│   ├── VactrolManager.h        # 6-ch LEDC engine (AUTO/manual/clamp/slew)
│   ├── RelayManager.h          # Sequencer + clock + ButtonBank decls
│   └── WebConsole.h            # AsyncWebServer + WebSocket
├── src/
│   ├── main.cpp                # Wiring, ~100 Hz loop, NVS load/save
│   ├── RadarParser.cpp         # UART parsing (+ SIM generator in sim build)
│   ├── DSP.cpp                 # DSP pipeline process()
│   ├── StateMachine.cpp        # Transitions, gamma/slew, Pi serial link
│   ├── VactrolManager.cpp      # LEDC outputs
│   ├── RelayManager.cpp        # Non-blocking sequences, boot ritual, buttons
│   └── WebConsole.cpp          # HTTP + WS, inline SPA console
└── pi/                         # Raspberry Pi playback stack
```

## Flashable Raspberry Pi Images (CI-built)

Preconfigured Raspberry Pi OS Lite (64-bit, Bookworm) images for **Pi 3/4/5**
are built automatically by GitHub Actions (`.github/workflows/build-images.yml`):
go to the repo's **Actions → Raspberry Pi Images** run → download artifact
(`apparatus-pi-A-img` / `apparatus-pi-B-img`, xz-compressed), flash with
Raspberry Pi Imager / balenaEtcher. Tagged releases attach both images.

| Image | Services enabled | Scans `/home/pi/media` for |
|-------|------------------|----------------------------|
| `apparatus-pi-A` | Layer 1 autoloader | `layer1_loop*.mp4` (fallback `layer1*`) |
| `apparatus-pi-B` | Master autoloader + ESP32 serial daemon | `master_L2_L3*.mp4` (fallback `master*`) |

Videolooper behavior on **both** Pis (`pi/media_autoloader.py`):
- Boot → scan media folder → gapless autoplay of newest matching file
  (`.mp4 .mkv .mov .avi .ts`)
- **Hot reload**: replace/add a matching file while running → player swaps to
  it within ~10 s — no reboot needed for content updates
- No media yet → gray placeholder card ("unit alive, awaiting media")
- Media folder is empty in the image by design; copy files via SSH/SFTP or USB

Pi B image additionally ships: stable PL011 UART for the ESP32 link
(`disable-bt` overlay, serial console stripped from the kernel cmdline),
`python3-serial`, and the `LOOP_A/LOOP_B/TRIGGER_SEEK` daemon.

Image login: user `pi`, password `apparatus`, SSH enabled — change before any
public exhibition build (`first-user-name` / `password` inputs in the workflow).

## Pi Playback Stack (Zero-Blackout Architecture)

### Serial protocol (replaces the old GPIO pulse trigger)
ESP32 UART1 TX (GPIO2) → Pi B RXD (`/dev/serial0`), 115200 8N1, common GND:

| Command | Meaning | mpv actions |
|---------|---------|-------------|
| `LOOP_A\n` | Layer 2 regime | A-B loop → 0–300 s |
| `LOOP_B\n` | Drift toward Layer 3 | A-B loop → 300–600 s (no seek) |
| `TRIGGER_SEEK\n` | CONTACT cut | loop → 300–600 s **then** `seek 300 absolute exact` **then** resume |

Loop points are set BEFORE the seek so playback can never be yanked back into
the Layer 2 window mid-transition. Exact keyframe seek = no blackout, no dropped
frames.

### Files (`pi/`)
| File | Role |
|------|------|
| `player_a.py` | Pi A: gapless Layer 1 loop via mpv `--loop-file=inf` |
| `player_b.py` | Pi B: launches mpv on `master_L2_L3.mp4`, seeds Layer 2 A-B loop |
| `mpv_daemon.py` | Pi B serial listener → IPC executor (`LOOP_A`/`LOOP_B`/`TRIGGER_SEEK`) |
| `test_mpv_daemon.py` | Mock-socket self-test of the full command protocol |
| `deploy_pi.sh` | One-shot SSH deploy of scripts + systemd units |
| `systemd/*.service` | Units for both Pis; watcher bound to player lifecycle |

### Deployment
```bash
./pi/deploy_pi.sh pi@<PI_B_IP> b
./pi/deploy_pi.sh pi@<PI_A_IP> a
python3 pi/test_mpv_daemon.py    # -> ALL TESTS PASS (no hardware needed)
```

## Bench Bring-Up Checklist

1. Flash production firmware, open monitor: expect
   `LD2410 initialized: Engineering Mode ON`, ACK-matched config, low parse errors
2. Join AP `TheApparatus_AP`, open console, watch telemetry while walking
3. Verify relay clicks: FIRE button → single click; set count=2 → double click
4. Remap a relay pin from the GUI → confirm click moves to the new GPIO
5. Set a clock interval → confirm periodic re-firing
6. Configure boot steps → REPLAY NOW → observe choreography
7. Touch the GPIO12 plate → CONTACT: mix snaps to 100%, Pi logs `TRIGGER_SEEK OK`
8. Save config, power-cycle → settings and boot ritual restored from NVS

## GUI Quality Assurance (Playwright)

`tools/gui-test/` contains a browser test suite that runs the **real firmware
SPA** against a mock ESP32 backend:

```bash
cd tools/gui-test
npm install && npx playwright install chromium
python3 ../extract_spa.py        # pull the SPA out of WebConsole.cpp verbatim
npx playwright test              # 14 tests, ~10 s
```

Coverage: WS connection & 20 Hz telemetry · state badge cycling · metric
updates · 9 gate bars + peak marker · radar preview canvas rendering ·
oscilloscope signal · vactrol cards · relay card controls · boot panel ·
FIRE/config commands reaching the WebSocket (asserted server-side) · save
payload integrity · zero JS errors under live telemetry.

## Research Notes & Design Justifications

- **LD2410 gate sensitivity**: per-gate sensitivity thresholds exist in the
  protocol (gate N sensitivity 0–100; 100 = off) — a future firmware option to
  mask interference sources behind walls. Not yet exposed.
- **mpv on Pi 5**: upstream issue #17447 reports dropped frames with
  `--vo=gpu --gpu-context=drm` on Pi 5. Both players accept
  `APPARATUS_MPV_VO_ARGS="--vo=gpu-next …"` to override the video output chain
  without code changes (Pi 3/4 defaults are unaffected).
- **Vactrol physics** (DAFx-23 power-balanced modeling literature): LDR turn-on
  is fast (tens of ms) while turn-off is slow (hundreds of ms), and the
  resistance-vs-LED-current curve is strongly nonlinear — this is exactly why
  the firmware applies gamma shaping plus slew limiting, and why each channel's
  min-clamp should be kept > 0 (an LED current floor keeps the LDR responsive;
  driving to true 0 mA makes releases sluggish). Calibrate γ per channel during
  bench bring-up.

## License

Proprietary — Studio Optika Si / The Apparatus Project