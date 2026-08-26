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

## ⏳ Pending

1. **H.264 encode params for frame-exact cuts** (P1)
   `ffmpeg h264 keyframe interval gop settings reliable frame exact seeking cut points archive master`
   → decides the re-encode recipe for the 10-min master so TRIGGER_SEEK lands
   on a keyframe at exactly t=300 s (GOP ≈ 1 s, closed GOP, no B-frame reordering
   across boundaries are the working hypotheses to verify).

2. **WJ-AVE5 button electrical scan** (P0 follow-up)
   Download the service manual PDF (Elektrotanya link in Research-Notes) and
   extract: button matrix vs direct-to-ground, keyer button latching behavior,
   safe tap points. Manual review beats web search here.

3. **LD2410 mounting / gallery coexistence** (P2)
   `24GHz mmWave radar false triggers fans HVAC metal reflections installation art mounting height angle`

4. **Bookworm/Wayland screen-blanking & EDID lock** (P2)
   `raspberry pi os bookworm wayland disable screen blanking headless edid force hdmi kiosk`

5. **Vactrol batch matching procedure** (P2, bench)
   `matching LDR vactrol channels calibration jig response curve measurement automation`

6. **Per-gate sensitivity write frames** (P3)
   Source: HLK-LD2410 datasheet §command table (0x64/0x68 family per ESPHome
   implementation) + ESPHome `ld2410` source on GitHub — read code, don't guess.

7. **Alternative: ESP32 native touch (GPIO 12/13?) as backup plate tech** (P3)
   `ESP32 touchRead capacitive metal plate distance reliability exhibition` —
   only if TTP223 disappoints on long wire runs.
