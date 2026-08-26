# Bench Bring-Up

First power-on checklist, in order. Do not skip ahead.

## 0. Flashing

```bash
pio run --target upload && pio device monitor --baud 115200   # production
pio run -e esp32dev_sim --target upload                        # SIM (no radar)
```

Boot log must show: `LD2410 initialized: Engineering Mode ON`, config ACKs
matched, parse error counter ~0, AP up.

## 1. Radar sanity (SIM first, then real)

- SIM build: watch the WebUI through a full synthetic cycle (approach →
  breathing → outlier absorbed at t=30 s → retreat).
- Real module: walk the room; gate bars should track you; peak-gate ◄ follows.

## 2. Touch / CONTACT

Short GPIO12 to GND is NOT needed - just touch the plate (or, to force CONTACT
without the plate, briefly bridge a jumper from GPIO12 to any clean GND while
watching the serial monitor): state snaps to CONTACT, mix PWM 100%,
Pi logs `TRIGGER_SEEK OK - LAYER 3 LIVE`. Release → state returns per radar.

## 3. Relays

1. GUI FIRE → single click on the wired button.
2. Set press_count=2 → double click. Adjust length/gap to taste.
3. Remap a relay pin from the dropdown → click moves to the new GPIO.
4. Clock mode → periodic re-fire.
5. Boot ritual: configure steps → REPLAY NOW → observe choreography.

⚠️ Relay GPIOs are active-LOW: a relay board that is *on* while ESP32 boots
means your board is active-HIGH — swap logic in `_writeLevel()` or use an
inverting board.

## 4. Vactrols

- Manual slider each channel; verify smooth motion across the full range.
- Keep min-clamp > 0 (LED floor keeps LDR responsive).
- Calibrate gamma γ per channel against the actual mixer response.

## 5. Pis

Flash images (see [[Raspberry-Pi-Images]]), drop correctly-named files into
`/home/pi/media`, confirm autoplay + hot-swap. Images output **PAL composite**
on the 3.5 mm AV jack (pre-configured); cable that into the mixer's channel
inputs. Wire PiLink TX→Pi RXD + common GND; daemon log shows `TRIGGER_SEEK OK`
on touch.

## 6. Persistence

Save config → power-cycle everything → all settings restored, boot ritual runs
by itself after the start delay.

## 7. Full dress rehearsal

Approach → watch layers dissolve → stand still → fader breathes with you →
touch → cut + camera key → release → walk away → system returns to Layer 1
regime on its own.
