# Research Queue (agent-reach / Exa)

Living checklist. Items marked ✅ are done — findings distilled into
`wiki/Research-Notes.md`. Pending items carry ready-to-run queries; Exa free
tier rate-limits, so run them next session or after adding an API key
(https://dashboard.exa.ai/api-keys → mcporter Exa MCP URL).

Run pattern:

```bash
~/.npm-global/bin/mcporter call exa.web_search_exa \
  query="<query below>" numResults=4
```

## ✅ Done (2026-08-26)

| Topic | Key finding | Where applied |
|-------|-------------|---------------|
| WJ-AVE5 internals | Service manual PDF exists (Elektrotanya, ~72 pp, schematics) + ManualsLib mirrors | wiki/Research-Notes |
| Vactrol sourcing | NSL-32SR2 (Advanced Photonix) in stock at Electrokit (~45 SEK): 40 Ω–5 MΩ, 5 ms rise / 80 ms fall, LED 25 mA max | wiki/Research-Notes |
| LD2410 tuning | ESPHome docs confirm per-gate move/still thresholds (0–100) + max gates as *number* entities; engineering-mode energies per gate | future FW feature spec |
| Touch frontend | TTP223 output is **push-pull** → GPIO34 needs NO external pull-down; mounts behind ≤ few mm non-conductive panel; 60 ms response; keep away from relay coils; 0.5 s power-on calib | wiki/Firmware pin note |
| Pi kiosk resilience | display-pi patterns: hardware `/dev/watchdog` + healthcheck cron + `Restart=always` + player self-relaunch | roadmap enhancement |
| (earlier) mpv Pi5 | #17447 frame drops → `APPARATUS_MPV_VO_ARGS` override shipped | players |

## ⏳ Still open

1. **WJ-AVE5 service manual deep-read** (P0, wiring phase)
   PDF located on 3 mirrors (Elektrotanya / freeservicemanuals.info /
   eserviceinfo.com) but ALL are CAPTCHA-gated for robots → needs one manual
   browser download by the user. Preliminary deep-read DONE from Operating
   Instructions via ManualsLib: control map + effect list + "Superimpose by
   Camera" confirmed; relay→button plan written in wiki/WJ-AVE5-Notes.md.
   Remaining: schematic-level button topology + keyer latching behavior.

2. ~~**ESP32-native touch backup**~~ → **DONE & PROMOTED**: user chose internal
   peripheral as PRIMARY. Implemented on T5/GPIO12 with hysteresis+drift
   baseline; TTP223 module no longer needed. See wiki/Research-Notes.md.

## ✅ Done (second pass, 2026-08-26)

| Topic | Key finding | Where applied |
|-------|-------------|---------------|
| H.264 frame-exact cuts | `keyint=N:min-keyint=N:no-scenecut` (N = fps) ⇒ guaranteed IDR every 1 s; IDR bars cross-GOP refs ⇒ t=300 s always instant-seekable | wiki/Research-Notes encode recipe |
| Radar gallery mounting | 2.2–2.8 m height, ~30° down-tilt; ceiling fans & moving curtains top false sources; no direct HVAC airflow | wiki/Research-Notes |
| Bookworm blanking | 3 layers (kernel/compositor/DPMS); kernel layer killed via `consoleblank=0` in cmdline.txt — automated by new stage `03-display-config` | shipped in image |
| Gate-sensitivity write frames | Extracted from ESPHome source (vendored): CMD **0x64**, payload `[00 00][gate u32LE][01 00][motion u32LE][02 00][still u32LE]`, inside open config session, then query-params + close | wiki/Research-Notes + vendor file |
| Vactrol batch matching | PantalaLabs/vactrol-tracer pattern: PWM sweep → ADC readout → curve compare; our ESP32 can be the jig itself | wiki/Research-Notes |

*(First-pass findings — WJ-AVE5 SM location, NSL-32SR2 sourcing, TTP223 push-pull,
LD2410 tuning entities, kiosk watchdog patterns — see first Done table above and
wiki/Research-Notes.md.)*
