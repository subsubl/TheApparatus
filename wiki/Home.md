# Home

Welcome to the wiki for **The Apparatus** — an interactive multimedia
installation by Studio Optika Si (working titles: *Safe Distance / Monument to
Capitulation / Brainwash*).

An ESP32 reads a 24 GHz mmWave radar, extracts the visitor's **breathing**, and
drives six vactrols that physically move the sliders of a circuit-bent
Panasonic **WJ-AVE5** analog video mixer. Eight relays press the mixer's effect
buttons. Touching the plate hard-cuts the video to Layer 3 *and* presses the
mixer's camera key, superimposing the visitor's face onto the faces of the
convicted, the oppressed, and the dead.

## The Three Layers (concept ↔ machine mapping)

| Layer | Name | Visitor state | Machine behavior |
|-------|------|---------------|------------------|
| 1 | *Algorithmic Anesthesia* | Far away / absent | Mixer shows pristine Layer 1 loop (Pi A → CH1). Mix fader at 0%. |
| 2 | *The Ground Truth* | Approaching | Radar distance drives the crossfade; relays circuit-bend effect buttons (tracking errors, analog rot). Pi B plays Layer 2 region. |
| 3 | *The Apparatus* | Touching the plate | Hard cut to Layer 3 montage (zero blackout), relay presses the camera key → face superimposition. |

The degradation of the image is therefore **not a digital effect**: it is the
analog hardware chain itself tearing the picture apart as you approach.

## Wiki pages

1. [[Architecture]] — signal flow, modules, state machine
2. [[Firmware]] — ESP32 code structure, DSP math, NVS configuration
3. [[WebUI]] — console panels, WebSocket protocol, calibration guide
4. [[Relay-System]] — WJ-AVE5 button spoofing, triggers, boot ritual
5. [[WJ-AVE5-Notes]] — mixer controls map, relay→button plan, deep-read status
6. [[Raspberry-Pi-Images]] — CI flashable images, videolooper behavior, PAL composite
7. [[Serial-Protocol]] — ESP32 ↔ Pi B command reference
8. [[Bench-Bring-Up]] — first power-on checklist and calibration
9. [[QA-and-Testing]] — Playwright GUI suite, mock server, self-tests
10. [[Research-Notes]] — protocol traps, mpv pitfalls, vactrol physics

## Quick links

- Repo: `github.com/subsubl/TheApparatus` (private)
- Flashable images: Actions → *Raspberry Pi Images* → latest run → artifacts
- Firmware build: `pio run` (production) / `pio run -e esp32dev_sim` (SIM)
