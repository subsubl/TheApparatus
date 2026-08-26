# WJ-AVE5 Integration Notes

Deep-read **done** — from the full Operating Manual
(`WJAVE5_OM_PANASONIC_EN_DE_FR_IT.pdf`, 74 pp, EN/DE/FR/IT, user-supplied;
OCR-extracted text at `docs/WJ-AVE5_OM_ocr.txt` in-repo). Service-manual
schematic level (button electrical topology) remains open — see bottom.

## What the machine is

Panasonic **WJ-AVE5 Digital AV Mixer**: two composite/YC inputs, mix + wipe
section with **built-in digital frame synchronizer**, digital effects,
superimposer (luminance keyer), fade control, 4-channel audio mixer.
The frame synchronizer means our two free-running Pi PAL feeds can be mixed
without external sync — confirmed by the Features section.

## Verified key facts (from the manual, page references to EN section)

1. **Every effect button is a latching ON/OFF switch.** The manual's phrasing
   for STROBE/MOSAIC/STILL/PAINT/P-IN-P is always "press again to exit" —
   and Superimpose has an explicit *master* "ON/OFF Switch (22)".
   ⇒ Our relay press = toggle. Whatever we turn ON must be turned OFF.
2. **Superimpose-by-Camera recipe** (pp. 16–17), exactly:
   - camera → `EXT CAMERA IN` (composite #64 or Y/C #63)
   - press `EXT CAMERA` key-source select (23) once — setup only
   - adjust `KEY LEVEL` (30) against a test card until clean key
     (white-on-black card → Key Level from upper range; black-on-white → lower)
   - runtime: press Superimpose ON/OFF (22) **once to engage**, **once again
     to disengage**
   - `REVERSE` (21) selects key polarity; `TITLE EFFECT` (29) cycles
     edge/shadow styles; Key Level does not apply to Character-Generator titles.
3. **EXT CAMERA accepts only live cameras, never VTR playback** (input notes)
   — ideal: our contact camera is live by definition; sync not required.
4. Mix bus: `Mix Mode Selection Switch (52)` + physical `Mix/Wipe Control
   lever (45)` — the lever is what vactrol channel 1 drives.
5. Digital effects per bus: STILL (13/14), STROBE (15/16), MOSAIC (17/18),
   PAINT (19/20) — each with A-bus and B-bus variants; plus P-IN-P (7),
   Multi-Wipe (8), One-way/Reverse wipe options.
6. Fade section: Video Fade (37) / Black (36) / White (35) switches + physical
   Fade Lever (39); audio follows video in fade if selected via (37)+(38).
7. Background colour: 8 colours incl. black/white, used by mix/wipe/superimpose/
   fader. Caution in manual: never press bus BACK COLOUR and superimpose BACK
   COLOUR simultaneously.

## Relay → button plan (final default, firmware-shipped)

| Relay | Name | Trigger | AVE5 target |
|-------|------|---------|-------------|
| 1 | WJ-BTN1 | Manual | STILL (freeze) |
| 2 | WJ-BTN2 | Manual | STROBE |
| 3 | WJ-BTN3 | Manual | MOSAIC |
| 4 | WJ-BTN4 | Manual | PAINT |
| 5 | WJ-BTN5 | Manual | WIPE arm |
| 6 | WJ-BTN6 | Manual | CUT (bus switch) |
| 7 | WJ-CAM | **Layer 3 Cut (Contact)** | SUPERIMPOSE (camera keyer) ON |
| 8 | WJ-CAM-OFF | **Layer 3 Release (un-latch)** | SUPERIMPOSE (camera keyer) OFF |

Because buttons latch, CONTACT entry presses the keyer ON (relay 7) and
CONTACT exit presses it OFF (relay 8). Both relays are wired **in parallel
across the same physical button pads** — two relays across one button is just
a parallel connection; each keeps its own timing/cooldown config. The new
"Layer 3 Release" trigger fires on any transition out of CONTACT.

(Trade-off: FADE lost its factory-default relay slot; assign it to any relay
from the GUI if you want motorized-fade moments.)

## Setup checklist (one-time, from the manual)

1. Camera → EXT CAM IN; press EXT CAMERA select (23).
2. Point camera at neutral panel, adjust KEY LEVEL (30) for clean key.
3. Choose REVERSE polarity so the visitor's face keys correctly.
4. Optional: TITLE EFFECT style for the key edge.
5. Leave Superimpose (22) OFF in the resting state — the apparatus toggles it.

## Choreography with verified semantics

- L2 entry: fire STROBE (relay 2) once → auto-press again after N ms to clear
  (set press_count=2!) or leave artists to clear manually via FIRE.
- Breath peaks: PAINT pulses (each fire = toggle → use even counts, or accept
  the flip as part of the aesthetic).
- CONTACT: relay 7 → face appears over Layer 3 montage; release → relay 8 →
  face gone. Fully hands-free, matches the latching reality.

## Electrical topology — ANSWERED (service manual, Switch Board schematic p42)

The SM was acquired and OCR-transcribed in full (72 pp; private local copy,
not redistributed). The switch board is a **scanned key matrix**:

- Scan lines **SCAN0–SCAN5** × key-return lines **KEY0–KEY7** run to the main
  board via connectors **CN2 / CN3** (10-pin).
- Every key cell carries a **series 1 kΩ resistor + isolation diode**
  (R31–R38 = "1K", D45/D46/D47/D48/D49… per row) — classic diode-isolated
  matrix, scanned by Panasonic custom gate array **MN188166CCP2**.
- Q1–Q12 on the switch board are the scan drivers/level shifters.

**Consequence for our wiring:**

1. **Relay across the button pads is the ONLY safe method.** Soldering a wire
   from a button pad "to ground" would short a driven scan line through the
   matrix diodes and corrupt unrelated keys — do not do it.
2. Our relays are invisible to the scanner when open; when closed they simply
   parallel the visitor-equivalent press. **No timing hazard**: a mechanical
   press is also "slow" relative to nothing — the scanner debounces itself;
   our default 120 ms press is comfortably above any sane debounce window
   (~10–50 ms typical for this era).
3. Never close two relays that share a scan line simultaneously with different
   intents? Not an issue either: diodes make cross-talk impossible — worst
   case two keys register at once (which is exactly what physically pressing
   two buttons does).

Verified schematic pages in this SM: Overall Block Diagram · Exploded View ·
Switch Board (p42) · Power Board (p44) · Video/REA boards · Replacement Parts
List. Local reference copies live outside the repo (license: personal use).

## Still open (needs service manual, not operating manual)

- ~~Button electrical topology~~ → **ANSWERED above** (diode key-matrix).
- Exact CN2/CN3 pinout table → available in the local PDF (p42) if we ever
  want to tap KEY/SCAN lines directly with an analog mux instead of relays.
