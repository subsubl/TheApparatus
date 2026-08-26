# Firmware

PlatformIO, Arduino framework, ESP32-WROOM-32. Envs:

- `esp32dev` — production (real radar)
- `esp32dev_sim` — `-DAPPARATUS_SIM_MODE`: synthetic 60 s scenario at true
  10 Hz (approach → MICRO breathing with 0.25 Hz wave → outlier injection at
  t=30 s absorbed by EMA → retreat). Zero cost in production binary.

## DSP pipeline (10 Hz)

1. **EMA** on raw distance, τ = 500 ms → α ≈ 0.181. Absorbs spikes/outliers.
2. **Peak-gate selection**: highest stationary-energy byte among the 9 gates
   becomes G_target (briefing spec).
3. **Sub-gate centroid interpolation**: α = (dist mod 75)/75;
   `E_virtual = (1−w)·E_peak + w·E_neighbor` where the neighbor is chosen in
   the direction of sway — kills the stair-step artifact when a viewer sways
   across a gate boundary.
4. **Biquad bandpass** 0.1–0.5 Hz, Direct Form I → zero-mean respiratory wave.
5. **AGC**: 40-sample (4 s) peak envelope; output strictly in [−1, +1].

## LD2410 protocol (verified against datasheet v1.02/v1.07)

- Engineering mode ON = command **0x62** (`0x61` is read-params — classic trap),
  OFF = 0x63. Config session must be opened/closed (`01 00` … `0xFE`) around it.
  Flag is volatile → re-applied every boot.
- Engineering frame payload order:
  `state(1) movDist(2) movE(1) stillDist(2) stillE(1) detDist(2)
  maxMovGate(1) maxStillGate(1) movingGates(9×1) stationaryGates(9×1)`,
  wrapped `AA … 55 00`, wire length 0x23. Gate energies are **bytes**.
- ACKs matched by echoed command word + status byte — no timing heuristics.

## Configuration & NVS

Everything user-tunable lives in one `CalibrationConfig` blob persisted via
`Preferences.h`: distances/hysteresis/gamma/M/slew/variance/breath-threshold/
Pi zones/PWM clamps, all 6 vactrol channel settings, all 8 relay definitions
(trigger, press shape, clock), GPIO remap table, boot sequence steps.
Factory reset wipes the namespace.

## Pin map (default)

See `PinDefinitions.h` (source of truth). Highlights: radar UART2 16/17 @256k,
vactrols 13/14/25/26/27/33 (10-bit LEDC), relays 4/18/19/21/22/23/32/15
(active-LOW, GUI-remappable), touch 34 (input-only!), PiLink TX GPIO2 @115200.

> GPIO 34/35/36/39 are input-only on ESP32 — never assign them to outputs.
