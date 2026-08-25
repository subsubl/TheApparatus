# The Apparatus — ESP32 Firmware

Firmware for "The Apparatus" — an interactive multimedia art installation using ESP32, HLK-LD2410 mmWave radar, vactrol-based video mixer crossfader control, and capacitive touch override.

## Hardware Architecture

| Component | Connection | Details |
|-----------|------------|---------|
| **MCU** | ESP32 DevKit v1 | 240 MHz, dual-core |
| **Radar** | HLK-LD2410 | UART2 (GPIO16 RX / GPIO17 TX), 256 kbps, Engineering Mode via cmd **0x62** |
| **Touch** | Capacitive plate | GPIO 4, hardware interrupt (CHANGE) |
| **Vactrol PWM** | LED + LDR crossfader | GPIO 18, LEDC channel 0, 5 kHz, 8-bit |
| **Pi Trigger** | Raspberry Pi B (mpv IPC) | GPIO 19, digital output |
| **FX Relays** | 8× Relay board | GPIOs 23,22,21,5,27,14,12,13 (stubbed) |
| **Status LED** | Onboard | GPIO 2 |

## Signal Flow

```
┌─────────────┐     ┌──────────────┐     ┌──────────────────┐     ┌─────────────┐
│  HLK-LD2410 │────▶│  EMA Filter  │────▶│ Gate Interpolation│────▶│  Biquad BP  │
│ (Engineering│     │  (500ms τ)   │     │  (centroid, α)   │     │ (0.1-0.5 Hz)│
│  Mode)      │     └──────────────┘     └──────────────────┘     └──────┬──────┘
└─────────────┘                                                           │
                                                                          ▼
┌─────────────┐     ┌──────────────┐     ┌──────────────────┐     ┌─────────────┐
│  State      │◀────│  AGC Norm.   │◀────│  Sliding Window  │◀────│  Biquad Out │
│  Machine    │     │  [-1, +1]    │     │  (4s, 40 samp)   │     └─────────────┘
└──────┬──────┘     └──────────────┘     └──────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────────────────┐
│  4-Tier State Machine                                       │
│  ┌────────┐   ┌────────┐   ┌────────┐   ┌──────────┐        │
│  │ IDLE   │──▶│ MACRO  │──▶│ MICRO  │   │ CONTACT  │        │
│  │ (0%)   │   │ Dist→PWM│   │ Breath │   │ Touch 100%│        │
│  └────────┘   └────────┘   └────────┘   └──────────┘        │
│       ▲                                                        │
│       │                    (touch interrupt)                   │
│       └───────────────────────────────────────────────────────┘
└─────────────────────────────────────────────────────────────┘
       │
       ▼
┌─────────────┐     ┌─────────────┐
│ Vactrol PWM │     │ Pi Trigger  │
│ (Crossfade) │     │ (mpv seek)  │
└─────────────┘     └─────────────┘
```

## DSP Pipeline Details

### 1. EMA Filter (500 ms time constant)
- Sample rate: 10 Hz (100 ms period)
- α = 1 - e^(-dt/τ) = 1 - e^(-0.1/0.5) ≈ 0.181
- Eliminates clothing noise, spike artifacts

### 2. Peak-Gate Selection + Sub-Gate Centroid Interpolation
- Gate size: 75 cm (9 gates = 0–675 cm), gate energies are **single-byte** values
- **Peak stationary-energy gate (G_target) selected** per master briefing
- Fractional position: α = (Distance mod 75) / 75
- Virtual energy blends G_target with its sway-direction neighbor:
  E_virtual = (1−w)·E_peak + w·E_neighbor
- Prevents step artifacts when the viewer sways across gate boundaries

### 3. Biquad Bandpass Filter (IIR)
- Passband: 0.1 Hz – 0.5 Hz (respiration range)
- Sample rate: 10 Hz
- Bilinear transform design
- Direct Form I implementation

### 4. Sliding-Window AGC
- Window: 40 samples (4 seconds @ 10 Hz)
- Tracks peak absolute envelope E_max
- Normalized output: N_resp = y[n] / (E_max + ε)
- Output strictly bounded: [-1.0, +1.0]

## LD2410 Protocol Notes (verified vs. datasheet v1.02/v1.07)

- **Engineering mode enable = `0x62`** (disable `0x63`). `0x61` is *read-parameters* — a common trap.
- Config sequence: `0xFF (value 01 00)` → `0x62` → `0xFE`. Engineering flag is volatile; re-applied at every boot.
- Engineering report payload (`type=0x01`, wire length `0x23`):
  `AA | state(1) | mov_dist(2) | mov_E(1) | still_dist(2) | still_E(1) | det_dist(2) | max_mov_gate(1) | max_still_gate(1) | moving_gates(9×1) | stationary_gates(9×1) | 55 00`
- Gate energies are **single bytes**, not uint16.
- `detection_distance` is the authoritative distance used by the pipeline.
- ACK frames: command header, echo cmd word, `01 00`, status (`0x00`=success). The driver matches ACKs to pending commands — no timing heuristics.

## State Machine

| State | Condition | Behavior |
|-------|-----------|----------|
| **IDLE (0)** | No target or distance > D_max + hysteresis | PWM → 0%, Pi trigger LOW |
| **MACRO (1)** | Target in [D_min, D_max], moving (variance > 5 cm) | Distance → PWM with gamma shaping + slew limit |
| **MICRO (2)** | Variance < 5 cm for > 1.5 s | Lock base PWM, add breathing modulation: P_out = P_base + N_resp × M × 255 |
| **CONTACT (3)** | Touch interrupt active | PWM = 255 (immediate), Pi trigger HIGH |

### Gamma Shaping
- Linearizes vactrol LDR response
- Mix_gamma = Mix_linear^γ
- γ > 1 expands dark range (typical for vactrols)

### Slew Rate Limiting
- Maximum PWM change per ms (configurable)
- Prevents jerky crossfader movement
- Bypassed in CONTACT state

## WebUI & Telemetry

### WebSocket Payload (20 Hz)
```json
{
  "type": "telemetry",
  "payload": {
    "state": 0-3,
    "state_name": "IDLE|MACRO|MICRO|CONTACT",
    "distance_raw": 150,
    "distance_filtered": 148.3,
    "stationary_energy": [12, 45, 234, 567, 89, 23, 12, 5, 1],
    "biquad_raw": 0.0234,
    "agc_normalized": 0.67,
    "pwm_output": 128,
    "base_pwm": 130.5,
    "gamma_shaped": 0.512,
    "timestamp_ms": 1234567
  }
}
```

### Configuration (NVS, persisted)
- `D_min`, `D_max` (cm) — distance mapping range
- `hysteresis` (cm) — state transition hysteresis
- `gamma_exponent` — vactrol linearization
- `breathing_depth_M` (0-1) — modulation depth in MICRO
- `slew_rate_limit` (PWM/ms) — inertia
- `pwm_min_clamp`, `pwm_max_clamp` (0-255) — output limits
- WiFi credentials for AP mode

## Building & Flashing

### Prerequisites
- PlatformIO Core (`pip install platformio`)
- ESP32 board support (`pio platform install espressif32`)

### Build
```bash
cd TheApparatus
pio run                      # production firmware (real radar)
pio run -e esp32dev_sim      # SIM MODE - synthetic scenario, no radar needed
```

### SIM MODE Scenario (esp32dev_sim)
60 s looping synthetic target at true 10 Hz, exercising the full pipeline:
- **0–12 s**: approach 420→140 cm → IDLE → MACRO (watch gamma-shaped PWM rise)
- **12–48 s**: stationary @ 150 cm ± 2 cm sway → MICRO lock after 1.5 s,
  gate energies oscillate at **0.25 Hz** (15 breaths/min) → visible breathing on the oscilloscope
- **48–60 s**: retreat 140→420 cm → MACRO → IDLE glide-down
- One +180 cm outlier spike at t=30 s per cycle — watch the EMA filter absorb it

Touch input remains real hardware: short GPIO 4 to 3V3 to fire CONTACT.

### Flash
```bash
pio run --target upload                          # production
pio run -e esp32dev_sim --target upload          # simulation
```

### Monitor
```bash
pio device monitor --baud 115200
```

### Filesystem (LittleFS for future web assets)
```bash
pio run --target buildfs
pio run --target uploadfs
```

## Project Structure

```
TheApparatus/
├── platformio.ini          # PlatformIO configuration
├── include/
│   ├── Config.h            # Pins, constants, data structures, calibration
│   ├── Radar.h             # LD2410 Engineering Mode driver
│   ├── DSP.h               # EMA, Gate Interpolation, Biquad, AGC
│   ├── StateMachine.h      # 4-tier state machine
│   ├── WebServer.h         # AsyncWebServer + WebSocket
│   └── FXRelay.h           # 8-relay stubbed controller
├── src/
│   ├── main.cpp            # Entry point, main loop, init
│   ├── Radar.cpp           # UART parsing, Engineering Mode frames
│   ├── DSP.cpp             # DSP pipeline process()
│   ├── StateMachine.cpp    # State transitions, PWM mapping
│   ├── WebServer.cpp       # HTTP + WebSocket, inline SPA
│   └── FXRelay.cpp         # Relay control stubs
└── data/                   # LittleFS files (future)
```

## Calibration Procedure

1. Connect to AP: `TheApparatus_AP` / `apparatus2024`
2. Open browser: `http://192.168.4.1`
3. Observe live telemetry:
   - **State badge** shows current mode
   - **Energy bars** show 9-gate stationary energy
   - **Oscilloscope** shows biquad (blue) and AGC (orange) traces
4. Adjust sliders/inputs:
   - Walk toward/away from radar to set `D_min`/`D_max`
   - Watch gamma curve response in MACRO state
   - Stand still to engage MICRO — adjust `breathing_depth_M`
   - Touch plate to test CONTACT override
5. Click **Save Configuration to NVS** — persists across reboots

## Pi B Integration (mpv IPC)

The `PI_TRIGGER_PIN` (GPIO 19) goes HIGH in CONTACT state. The production
implementation lives in `pi/trigger_watcher.py` (see "Pi Playback Stack"
section below) - deploy it as a systemd service rather than using ad-hoc scripts.

## Pi Playback Stack (Zero-Blackout Architecture)

### Files (`pi/`)
| File | Role |
|------|------|
| `player_a.py` | Pi A: gapless Layer 1 loop via mpv `--loop-file=inf` |
| `player_b.py` | Pi B: launches mpv on `master_L2_L3.mp4`, seeds A-B loop to Layer 2 (00:00–05:00) |
| `trigger_watcher.py` | Pi B daemon: polls ESP32 GPIO trigger (BCM19 / physical pin 35), fires the Layer 3 cut over IPC |
| `test_trigger_cut.py` | Self-test: mock mpv socket validates exact command sequence (run anywhere, no hardware) |
| `deploy_pi.sh` | One-shot deploy: copies files + installs systemd units over SSH |
| `systemd/*.service` | Units for both Pis, auto-restart, graphical.target |

### The Cut Sequence (trigger_watcher.py)
On rising edge of GPIO 19 (ESP32 CONTACT state), with 5 s cooldown:
1. `set_property ab-loop-a 300`
2. `set_property ab-loop-b 600`
3. `seek 300 absolute+exact` — lands precisely on the Layer 3 keyframe boundary
4. `set_property pause false`

Loop points are set BEFORE the seek so playback can never be yanked back into
the Layer 2 window mid-transition. Because Layers 2/3 share one contiguous
file and the seek target is an exact keyframe position, mpv switches without
decoder teardown — no blackout, no dropped frames.

### Deployment
```bash
# Pi B (master + trigger watcher)
./pi/deploy_pi.sh pi@<PI_B_IP> b

# Pi A (layer 1 loop)
./pi/deploy_pi.sh pi@<PI_A_IP> a

# Verify
ssh pi@<PI_B_IP> systemctl status apparatus-player-b apparatus-trigger-watcher

# Test cut logic without a Pi (mock mpv IPC server):
python3 pi/test_trigger_cut.py --mock   # -> ALL TESTS PASS
```

Wiring note: **Pi GND must be common with ESP32 GND** for the GPIO trigger to read correctly.

## Future Expansion (FX Relays)

The 8 relays are stubbed with placeholder effects:
- `FX_EFFECT_STROBE` — rapid switching
- `FX_EFFECT_RAMP` — gradual fade (needs PWM/DAC)
- `FX_EFFECT_SEQUENCE` — sequential activation
- `FX_EFFECT_RANDOM` — stochastic patterns
- `FX_EFFECT_AUDIO_REACTIVE` — audio sync (future)
- `FX_EFFECT_VIDEO_SYNC` — frame-accurate sync (future)

Each relay has configurable name, description, and default state in `Config.h`.

## License

Proprietary — Studio Optika Si / The Apparatus Project