# QA and Testing

Everything testable without hardware is tested automatically.

## Playwright GUI suite (`tools/gui-test/`)

Runs the **real SPA extracted from the firmware source** (`tools/extract_spa.py`)
against `mock_esp32.js` — a Node server emulating the ESP32's HTTP + WebSocket
behavior (20 Hz synthetic telemetry cycling MACRO/MICRO, full config document,
server-side recording of every client command).

```bash
cd tools/gui-test
npm install && npx playwright install chromium
python3 ../extract_spa.py
npx playwright test        # 14 tests, ~10 s
```

| # | Asserts |
|---|---------|
| 01 | WS connection badge reaches "Connected" |
| 02 | State badge renders live states from telemetry stream |
| 03 | Distance metrics update with plausible values |
| 04 | Exactly 9 gate bars + peak marker present |
| 05 | Radar preview canvas actually paints pixels (ImageData probe) |
| 06 | Oscilloscope has signal |
| 07 | Six vactrol cards w/ AUTO toggle + manual slider |
| 08 | Eight relay cards, every control present |
| 09 | Boot panel: 12 step cards + REPLAY button |
| 10 | FIRE click produces `relay_fire {index:0}` on the wire |
| 11 | Press-count edit produces correct `relay_cfg` payload |
| 12 | Calibration slider moves its value label |
| 13 | Save posts complete payload (6 vactrols, boot steps) |
| 14 | Zero page JS errors during 3 s of live telemetry |

The suite doubles as a regression harness: any SPA change re-runs against the
same protocol contract the firmware implements.

## Other tests

- `python3 pi/test_mpv_daemon.py` — mock-IPC validation of the serial command
  handling (order-sensitive TRIGGER_SEEK check) → ALL TESTS PASS.
- Firmware builds: `pio run -e esp32dev` and `-e esp32dev_sim` both green in CI
  of record (RAM 14%, Flash ~65%).
- Media autoloader: newest-wins / stem-class / empty-dir unit checks pass.

## Not yet automatable (hardware-bound)

Relay click audibility, vactrol travel vs mixer feel, LD2410 mount orientation,
touch plate sensitivity, end-to-end video chain timing. See [[Bench-Bring-Up]].
