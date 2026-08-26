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

## Still open (needs service manual, not operating manual)

- Button electrical topology: direct-to-ground vs scanned matrix → decides
  solder-tap points vs button-terminal bridging.
- Scan-rate/bounce tolerance (if matrix-scanned) → validates 120 ms press.
