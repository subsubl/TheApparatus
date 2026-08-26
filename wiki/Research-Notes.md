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

## Touch frontend decision

TTP223-class module drives GPIO34: its output is **push-pull**, so the usual
"input-only pins need external resistors" caveat does NOT apply — no pull-down
needed. Practical notes from component guides: mounts behind non-conductive
panels up to a few millimeters, ~60 ms response both edges, auto-calibrates
0.5 s after power-on (don't touch during boot), keep it physically away from
relay coils and long runs parallel to mains. Mode pads: default momentary +
active-HIGH is exactly what CONTACT edge-detection expects.

## Sourcing (checked 2026-08-26)

- **Vactrols**: NSL-32SR2 (Advanced Photonix/Luna) — Electrokit Sweden stock,
  ~45 SEK ea: 40 Ω on / 5 MΩ off, rise 5 ms / fall 80 ms, LED 25 mA @ 2.5 V,
  LDR up to 60 V / 50 mW. Rise/fall asymmetry matches the DAFx modeling above;
  order ≥ 10 for channel matching.
- **WJ-AVE5 service manual**: Elektrotanya carries the SM (schematics, ~72 pp);
  ManualsLib mirrors SM + Operating Instructions. Use it to plan relay tap
  points and settle the keyer-button latching question before wiring.

## Kiosk resilience patterns (from fielded Pi displays)

Layered self-healing worth adopting if the installation must run unattended
for weeks: hardware watchdog (`/dev/watchdog` + systemd `WatchdogSec`),
external healthcheck cron writing a status JSON, `Restart=always` on all
player units (we have this), player self-relaunch on decode stall. Our
autoloader already covers crash-relaunch; watchdog integration is a cheap
future add.

## Frame-exact cuts: master encode recipe

TRIGGER_SEEK must land on a keyframe at exactly t=300 s. Encode the master so
an **IDR frame exists every second** (N = fps):

```bash
# 25 fps (use keyint=30:min-keyint=30 for 30 fps)
ffmpeg -i master_in.mov -c:v libx264 -pix_fmt yuv420p -crf 18 -preset slow \
  -x264-params keyint=25:min-keyint=25:no-scenecut \
  -c:a aac -b:a 256k master_L2_L3.mp4
```

- `keyint=min-keyint=N` forces uniform GOP length — no encoder whimsy.
- `no-scenecut` forbids extra scene-cut I-frames (keeps grid exact).
- Default x264 I-frames are IDR → no references across GOP boundaries →
  seeking to t=300 starts decoding cleanly at that frame. Zero-blackout cut
  preserved.
- Verify before shipping: `ffprobe -select_streams v -show_frames` around
  t=300 and confirm `key_frame=1` exactly there.

## LD2410 per-gate sensitivity — write protocol (from ESPHome source)

Spec extracted from ESPHome's implementation (vendored at
`docs/vendor/esphome_ld2410.cpp`, MIT): within an open config session,
send command **0x64** (`CMD_GATE_SENS`) with the 18-byte payload

```
[00 00] [gate u32 LE] [01 00] [motion_thr u32 LE] [02 00] [still_thr u32 LE]
```

then `query_parameters()` and close the session (same open/close discipline as
our engineering-mode toggle). Thresholds are 0–100; ~100 effectively disables a
gate — exactly what we need to mask ceiling fans or wall-mounted interference
per gallery. Planned feature: WebConsole "Gate sensitivity" panel writing all
9 gate pairs through RadarParser's existing command layer.

## Gallery mounting (radar)

Field consensus: mount **2.2–2.8 m high, tilted down ~30°**. Top real-world
false-trigger sources are **ceiling fans** (periodic Doppler) and **moving
curtains**; avoid direct HVAC airflow and metal surfaces inside the cone.
Per-gate threshold masking (above) is the sanctioned cure when geometry can't
avoid a nuisance sector.

## Display blanking (Bookworm Lite images)

Three independent blanking layers exist: kernel console, compositor idle
(n/a — we run none), monitor DPMS. The kernel layer is now neutralized in the
images itself: stage `image/common-stack/03-display-config` appends
`consoleblank=0` to `/boot/firmware/cmdline.txt`. If a venue display misbehaves
with EDID, force modes manually via e.g. `video=HDMI-A-1:1920x1080@60D` on the
same line.

## Vactrol batch matching (bench jig)

Pattern per PantalaLabs/vactrol-tracer: sweep LED current (PWM), read the LDR
divider with an ADC, plot R vs duty. Any spare ESP32 does this natively
(LEDC + ADC). Procedure: order ≥10 NSL-32SR2, trace each unit, group channels
with matching curves, install matched pairs into symmetric controls (Color X/Y),
record each channel's curve → derive its γ for the firmware clamp/gamma table.
