# WJ-AVE5 Integration Notes

Everything known about the mixer itself, consolidated. Deep-read of the
service manual PDF is queued (needs one manual browser click — see bottom).

## What the machine is

Panasonic **WJ-AVE5 Digital AV Mixer**: two composite/S-video inputs, mix +
wipe section, digital effects, superimposer (keyer), fade control, audio
mixer. Composite I/O pairs perfectly with our Pi PAL-composite feeds.

## Verified documentation sources

| Document | Where | Status |
|----------|-------|--------|
| Operating Instructions (~21 pp) | ManualsLib (product page 3961369) | ✅ readable online, ToC extracted |
| Service Manual (72 pp, schematics) | Elektrotanya · freeservicemanuals.info · eserviceinfo.com | ⏳ CAPTCHA-gated for robots → needs your browser |

## Control surface map (from Operating Instructions ToC)

Pages refer to the Operating Instructions:

- p5 **Major Operating Controls** — the physical button/slider layout
- p6–10 **Wipe Patterns**
- p12 **MIX Effect, Wipe Mode** — the mix/wipe bus our Mix vactrol drives
- p15–16 **Digital Effects**: `STILL` · `CUT`/Multi-Wipe · `STROBE` · `MOSAIC` · `PAINT`
- p17 **Superimpose Effect & Back Colour** → includes **“Superimpose by
  Camera”** — *the* camera-keyer function our relay 7 (`WJ-CAM`) presses on
  CONTACT. Also here: Fade Control, Reverse Effect, Title Effect
- p18 Fading Operation · p19 Audio Mixer

## Relay → button assignment (working plan, 8 channels)

| Relay | Default name | Proposed AVE5 target | Used by |
|-------|--------------|---------------------|---------|
| 1 | WJ-BTN1 | `STILL` (freeze) | Boot ritual “power-click”; L2 glitch stabs |
| 2 | WJ-BTN2 | `STROBE` | L2 entry — attention-grabbing stutter |
| 3 | WJ-BTN3 | `MOSAIC` | L2 deep-degradation beat |
| 4 | WJ-BTN4 | `PAINT` | Breath-peak (inhale) color smear |
| 5 | WJ-BTN5 | `WIPE` pattern select | L1→L2 transition accent |
| 6 | WJ-BTN6 | `CUT` (hard bus switch alt.) | spare/performance |
| 7 | WJ-CAM | **`SUPERIMPOSE` (camera key)** | fires automatically on CONTACT |
| 8 | WJ-BTN8 | `FADE` | exhibition open/close ritual |

All assignments are GUI-editable per installation day — this table is the
sensible default, not a constraint.

## Choreography sketch with real effects

- **L1 (far)**: clean loop, zero effects.
- **L2 entry (MACRO)**: brief `STROBE` stab → release; as visitor nears,
  `MOSAIC` holds longer; breath inhale-peaks pulse `PAINT`.
- **CONTACT (touch)**: `WJ-CAM` presses Superimpose-by-Camera while Pi B seeks
  to t=300 s — visitor’s live face rides over the L3 montage.
- **Boot ritual**: FADE-in from black after mixer PSU settles, then arm STILL.

## Electrical questions for the service-manual deep-read ⏳

1. Button topology: direct-to-ground vs scanned matrix (determines whether a
   relay can simply bridge pads, or must emulate a row/column short).
2. Is SUPERIMPOSE latching or momentary? If latching → set relay 7 to a single
   long press on entry AND exit (GUI press-count/length handles it; no FW change).
3. Safe tap points & voltages on the Main Board (p12+) — solder pads preferred
   over button terminals where possible.
4. Any MCU scanning that could be confused by slow relay bounce → measure, and
   if needed raise press_length_ms ≥ 80 ms (default 120 ms should be safe).

## PAL composite chain reminder

Pi A → AV jack → mixer IN 1 · Pi B → AV jack → mixer IN 2 · mixer OUT →
projector/monitor. All PAL (`sdtv_mode=2`, baked into images). Keep cables
short and away from relay/vactrol wiring; composite hates ground loops —
star-ground at the mixer PSU.
