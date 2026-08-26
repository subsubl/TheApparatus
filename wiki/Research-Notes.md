# Research Notes

Decisions grounded in sources; traps found the hard way. See also README
"Research Notes & Design Justifications".

## LD2410

- Engineering mode is command **0x62**, not 0x61 (read-parameters). Verified
  against HLK datasheet v1.02/v1.07, ESPHome sources, reference parsers.
- Gate energies are single bytes; payload order documented in [[Firmware]].
- Per-gate **sensitivity thresholds** (0–100, 100 = gate off) exist in the
  protocol — candidate future feature to mask wall-mounted interference.
- `detection_distance` field is authoritative over per-type distance fields.

## mpv on Raspberry Pi

- Upstream issue mpv-player/mpv#17447: `vo=gpu + gpu-context=drm +
  gpu-api=opengl` drops frames on Pi 5 even for FHD content on some builds.
  Mitigation shipped: `APPARATUS_MPV_VO_ARGS` env override (e.g.
  `--vo=gpu-next`) without touching code. Pi 3/4 defaults unchanged.
- Zero-blackout cut technique: set new A-B points BEFORE exact seek within one
  contiguous file — no decoder teardown (see [[Serial-Protocol]]).
- Adafruit video-looper lineage (omxplayer) is dead; modern forks use mpv —
  our autoloader implements the same UX natively on Bookworm.

## Vactrol physics (DAFx-23 power-balanced modeling, EDN exponential drive)

- Attack (tens of ms) ≪ release (hundreds of ms); attack speeds up with LED
  current. Consequences implemented:
  - gamma shaping compensates static nonlinearity,
  - slew limiting hides release sluggishness during fast transitions,
  - min-clamp > 0 keeps releases responsive (never true 0 mA),
  - CONTACT bypasses slew: hard cuts are supposed to be hard.
- LDR conductance-vs-current is more linear than resistance — relevant if we
  ever add a second calibration curve mode.

## pi-gen / GitHub Actions (learned by doing)

- Branch names: `bookworm-arm64` etc.; no `armbookworm`.
- Timezone input validated against npm `countries-and-timezones`; aliases like
  Europe/Ljubljana are absent → use Europe/Vienna.
- `increase-runner-disk-size` purges a hardcoded apt package list that breaks
  ARM runners → do rm-based cleanup instead.
- Custom stages: need `prerun.sh`+`copy_previous`, must be self-contained
  (bundle files at CI time), chroot via stdin heredoc only.

## ESP32 pins

GPIO 34–39 input-only, no pull-ups. GPIO 6–11 flash. strapping pins 0/2/5/12/15
careful at boot (relay board default-off choice mitigates 12/15 concerns).
