# Relay System

Eight relays are wired **across** the WJ-AVE5's buttons: closing a relay is
electrically identical to a finger pressing that button. The board is
active-LOW (GPIO LOW = pressed).

## Trigger sources

| Trigger | Fires when |
|---------|-----------|
| Manual | GUI FIRE / performer button |
| Layer 1 Return (Idle) | State machine enters IDLE |
| Layer 2 Entry (Macro) | Enters MACRO |
| Breath Lock (Micro) | Enters MICRO |
| Layer 3 Cut (Contact) | Enters CONTACT — **default of relay 7 (`WJ-CAM`) = camera key press** |
| Inhale / Exhale Peak | AGC breathing wave crosses +thr/−thr (edge-latched, cooldown-guarded) |

State-linked triggers fire once per transition; breath triggers have a shared
configurable cooldown (`auto_trigger_cooldown_ms`) so rapid breathing can't
machine-gun the buttons.

## Press shaping

Per relay: `press_length_ms` (30–3000), `press_count` 1–5 (double/triple
clicks!), `press_gap_ms`. Sequences run in a deadline-based scheduler — zero
blocking. Clock mode re-fires the sequence every N ms whenever idle.

## GPIO remapping

Every relay's pin can be changed live from the GUI dropdown. Conflicts with
other relays and protected pins (UART, flash, PiLink) are rejected with a
`pin_fail` ack. Remaps take effect immediately (old pin released HIGH/safe).

## Performer buttons

Physical buttons on the input bank: 1 click = configured sequence, 2–3 clicks =
one extended press (×2/×3 length, capped 2 s), long-press = momentary hold.
Input-only pins need external pull-downs.

## Boot ritual

On power-up: wait start delay (mixer PSU!) → execute steps sequentially.
Each step: select relay, press N times (length/gap), dwell. Defaults press
BTN1 long (power-on) then arm an effect. Everything editable/persisted;
"REPLAY NOW" re-runs without rebooting.

## Wiring notes

- Relay contacts across the button pads; keep leads short, mixer ground float.
- If a mixer button turns out to be latching, swap which physical pad the
  relay bridges or change its behavior via GUI — no firmware edits needed.
- Common ESP32↔mixer-ground is NOT required (pure dry-contact closure).
