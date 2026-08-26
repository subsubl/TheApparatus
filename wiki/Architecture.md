# Architecture

```
HLK-LD2410 ─UART2─▶ RadarParser ─▶ EMA(500ms) ─▶ PeakGate+Centroid ─▶ Biquad(0.1–0.5Hz) ─▶ AGC[-1,+1]
                                                                                        │
Touch(GPIO34) ──ISR──┐                                                                  │
                     ▼                                                                  ▼
              ╔══════════════════════ 4-TIER STATE MACHINE ════════════════════╗
              ║ IDLE(0%) ⇄ MACRO(dist^γ + slew) ⇄ MICRO(P_base + breath·M)    ║
              ║                    CONTACT = hard override (100% + L3 cut)     ║
              ╚═══╦═══════════════╦══════════════╦════════════════╦════════════╝
                  ▼               ▼              ▼                ▼
          6× Vactrol PWM   8× Relay seq    BootSequencer    UART1 "TRIGGER_SEEK"
          (mixer sliders)  (+clock mode)   (power-on ritual)     → Pi B mpv IPC
```

## Modules

| Module | Files | Responsibility |
|--------|-------|----------------|
| PinDefinitions | `include/PinDefinitions.h` | Pin map, enums, CalibrationConfig (NVS blob), BootSequencer decl |
| RadarParser | `src/RadarParser.cpp` | LD2410 engineering-mode frames @10 Hz (+ synthetic generator in SIM builds) |
| DSPPipeline | `src/DSP.cpp` | EMA → gate selection + centroid interpolation → biquad → AGC |
| ApparatusStateMachine | `src/StateMachine.cpp` | 4 states, gamma/slew PWM, hysteresis, serial link |
| VactrolManager | `src/VactrolManager.cpp` | 6× LEDC channels, AUTO/manual, clamps, per-channel slew |
| RelayManager + ButtonBank | `src/RelayManager.cpp` | Non-blocking press sequencer, clock mode, auto-triggers, performer buttons |
| BootSequencer | (in RelayManager.cpp) | Power-on button choreography |
| WebConsole | `src/WebConsole.cpp` | AP + HTTP + WebSocket + inline SPA |

## State machine transitions

```
IDLE  --target enters [D_min, D_max]-->            MACRO
MACRO --variance < thr for lock_time-->            MICRO
MACRO --distance > D_max + hysteresis-->           IDLE     (bidirectional hysteresis)
MICRO --variance >= thr-->                         MACRO
MICRO --distance > D_max + hysteresis-->           IDLE
ANY   --touch-->                                   CONTACT
CONTACT --touch released-->                        radar decides (MICRO/MACRO/IDLE)
```

- **IDLE**: mix target 0%, Layer 1 regime. `LOOP_A` sent if returning from far zone.
- **MACRO**: normalized distance (0 at D_max → 1 at D_min), gamma-shaped,
  slew-limited. Near zone sends `LOOP_B`.
- **MICRO**: base locked, breathing modulation added:
  `P_out = P_base + N_resp × M`. Breath peaks fire relay triggers.
- **CONTACT**: instant 100%, single `TRIGGER_SEEK`, relay(s) with
  *Layer 3 Cut* trigger fire (camera key).

## Video chain

Pi A loops Layer 1 into mixer CH1 continuously. Pi B plays the contiguous
master file (L2 = 00:00–05:00, L3 = 05:00–10:00) into CH2, A-B looped to the
Layer 2 region. The mix between CH1/CH2 is the physical T-bar, moved by
vactrol channel 0.
