# WebUI

Connect to AP **`TheApparatus_AP`** → open `http://192.168.4.1`.
Single-page console served by the ESP32 itself; WebSocket telemetry at 20 Hz.

## Panels

1. **System state** — state badge (IDLE/MACRO/MICRO/CONTACT color-coded), mix
   PWM, Pi trigger, raw/filtered distance, base PWM, gamma output.
2. **Radar gates** — 9 stationary-energy bars, ◄ marks the peak gate used by
   the DSP centroid.
3. **Radar Preview** — top-down room view: LD2410 at bottom-center, gate arcs
   every 75 cm, red D_min/D_max band, blue Pi-zone lines, live target dot:
   - amber = MACRO approach
   - green + ♥ = MICRO breathing lock (dot pulses with the breath)
   - gray ghost = IDLE last-known position
   - "CONTACT – live camera" overlay while touched
4. **Breathing oscilloscope** — dual trace: biquad raw (blue) and AGC
   normalized ±1 (orange), 20 s rolling window.
5. **Vactrol channels ×6** — per channel: AUTO/MANUAL toggle, **"Drives"
   dropdown naming the physical WJ-AVE5 pot/lever this channel actuates**
   (Mix/T-Bar, Color X/Y, Wipe speed, Effect level, Fade lever, Audio level),
   manual level slider, min/max clamps, slew. Mix/T-Bar is radar-driven in AUTO.
6. **Sensor calibration** — range sliders for every DSP/state threshold;
   *Save to NVS* persists everything (incl. relay/boot config).
7. **Relay bank ×8** — per relay: name, trigger dropdown, press length/count/
   gap, clock toggle + interval, GPIO remap dropdown, FIRE/STOP, live dot.
8. **Boot sequence editor** — 12 steps with use-checkboxes; per step: relay,
   presses 1–5, length, gap, dwell-after. Start delay slider. REPLAY NOW.

## WebSocket protocol

Server → client:

```jsonc
{ "type": "telemetry", "payload": {
    "state": 2, "state_name": "MICRO",
    "distance_raw": 153, "distance_filtered": 150.4,
    "stationary_energy": [12,45,234,567,89,23,12,5,1], "peak_gate": 3,
    "biquad_raw": 0.023, "agc_normalized": 0.67,
    "mix_pwm": 138, "base_pwm_f": 136, "gamma_shaped": 0.55,
    "pi_trigger": false,
    "relay_pressed": [0,0,0,0,0,0,0,0], "relay_seq": [0,0,0,0,0,0,0,0],
    "vactrol_val": [128,64,64,90,10,77] } }

{ "type": "config", "payload": { /* full CalibrationConfig */ } }
{ "type": "saved" | "pin_ok" | "pin_fail" }
```

Client → server: `get_config`, `relay_fire {index}`, `relay_stop {index}`,
`relay_cfg {index, trigger?, press_length_ms?, press_count?, press_gap_ms?,
clock_enable?, clock_interval_ms?}`, `relay_pin {index, pin}`,
`vactrol_auto {ch, auto}`, `vactrol_manual {ch, value}`,
`save_config {payload}`, `boot_replay`, `factory_reset`.

All incoming values are constrained server-side before touching the config.

## Calibration walkthrough

1. Watch Radar Preview while walking toward/away: set D_max where presence
   should first matter, D_min where the image should be fully degraded.
2. Hysteresis ~10–20 cm stops boundary flicker.
3. Stand still → MICRO engages after lock time; tune γ so the MACRO fade feels
   linear on the actual vactrol (bench-test, LDRs vary wildly).
4. Breathing depth M: oscilloscope shows your wave; M scales it onto the fader.
