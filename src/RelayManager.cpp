/**
 * @file RelayManager.cpp
 * @brief Non-blocking relay sequencer + clock + boot ritual + button interpreter
 *
 * Electrical polarity: active-LOW board. LOW = pressed, HIGH = released.
 * All timing deadline-based; zero delay() calls.
 */

#include "RelayManager.h"
#include <Arduino.h>

/* ============================================================================
 * INIT
 * ============================================================================ */

void RelayManager::begin() {
    for (uint8_t i = 0; i < g_config.relay_count && i < BANK_CAPACITY; i++) {
        int pin = g_config.layout.relay_pins[i];
#ifndef APPARATUS_NATIVE_TEST
        if (pin >= 0) {
            pinMode(pin, OUTPUT);
            digitalWrite(pin, RELAY_BOOT_SAFE_LEVEL);
        }
#endif
        _rt[i] = FxRelayRuntime{};
    }
    log_i("RelayManager: %d channels (active-LOW, released)", g_config.relay_count);
}

/* ============================================================================
 * GPIO REMAPPING & DYNAMIC ALLOCATION
 * ============================================================================ */

bool RelayManager::remapPin(uint8_t index, int new_pin) {
    if (index >= g_config.relay_count || index >= BANK_CAPACITY) return false;
    int old_pin = g_config.layout.relay_pins[index];
    AllocationResult ar = apparatus::remapOutput(g_config.layout, Bank::Relay, index, new_pin);
    if (!ar.ok) {
        log_w("Remap relay %u -> GPIO %d failed: %s", index, new_pin, ar.error);
        return false;
    }
    stopSequence(index);
#ifndef APPARATUS_NATIVE_TEST
    if (old_pin >= 0) {
        digitalWrite(old_pin, RELAY_BOOT_SAFE_LEVEL);
        pinMode(old_pin, INPUT);
    }
    if (new_pin >= 0) {
        pinMode(new_pin, OUTPUT);
        digitalWrite(new_pin, RELAY_BOOT_SAFE_LEVEL);
    }
#endif
    log_i("Relay %u remapped: GPIO %d -> GPIO %d", index, old_pin, new_pin);
    return true;
}

bool RelayManager::addRelay(int pin) {
    AllocationResult ar = (pin < 0) ? apparatus::addOutput(g_config.layout, Bank::Relay)
                                    : apparatus::addOutput(g_config.layout, Bank::Relay, pin);
    if (!ar.ok) {
        log_w("addRelay failed: %s", ar.error);
        return false;
    }
    g_config.relay_count = g_config.layout.relay_count;
    uint8_t idx = g_config.relay_count - 1;
    g_config.fx[idx] = FxRelaySettings{};
    g_config.fx[idx].enabled = true;
    int new_pin = g_config.layout.relay_pins[idx];
#ifndef APPARATUS_NATIVE_TEST
    if (new_pin >= 0) {
        pinMode(new_pin, OUTPUT);
        digitalWrite(new_pin, RELAY_BOOT_SAFE_LEVEL);
    }
#endif
    _rt[idx] = FxRelayRuntime{};
    log_i("Relay channel %u added on GPIO %d (total %d)", idx, new_pin, g_config.relay_count);
    return true;
}

bool RelayManager::removeLastRelay() {
    if (g_config.relay_count <= 0) return false;
    uint8_t idx = g_config.relay_count - 1;
    int old_pin = g_config.layout.relay_pins[idx];
    AllocationResult ar = apparatus::removeLastOutput(g_config.layout, Bank::Relay);
    if (!ar.ok) {
        log_w("removeLastRelay failed: %s", ar.error);
        return false;
    }
    stopSequence(idx);
#ifndef APPARATUS_NATIVE_TEST
    if (old_pin >= 0) {
        digitalWrite(old_pin, RELAY_BOOT_SAFE_LEVEL);
        pinMode(old_pin, INPUT);
    }
#endif
    _rt[idx] = FxRelayRuntime{};
    g_config.fx[idx] = FxRelaySettings{};
    g_config.relay_count = g_config.layout.relay_count;
    log_i("Relay channel %u removed from GPIO %d (total %d)", idx, old_pin, g_config.relay_count);
    return true;
}

/* ============================================================================
 * UPDATE
 * ============================================================================ */

void RelayManager::update(bool state_changed, ApparatusState_t new_state, float agc, float velocity_cm_s) {
    static ApparatusState_t s_prev_state = STATE_IDLE;
    ApparatusState_t prev = s_prev_state;
    for (uint8_t i = 0; i < g_config.relay_count && i < BANK_CAPACITY; i++) {
        _sequencerTick(i);
        _clockTick(i);
    }
    _autoTriggerCheck(state_changed, prev, new_state, agc, velocity_cm_s);
    s_prev_state = new_state;
}

/* ============================================================================
 * SEQUENCED FIRING
 * ============================================================================ */

void RelayManager::fireSequence(uint8_t index) {
    if (index >= g_config.relay_count || index >= BANK_CAPACITY || !g_config.fx[index].enabled) return;
    FxRelaySettings& s = g_config.fx[index];
    fireSequenceCustom(index, s.press_length_ms, s.press_count, s.press_gap_ms);
}

void RelayManager::fireSequenceCustom(uint8_t index, uint16_t length_ms,
                                      uint8_t count, uint16_t gap_ms) {
    if (index >= g_config.relay_count || index >= BANK_CAPACITY || !g_config.fx[index].enabled) return;
    FxRelayRuntime& rt = _rt[index];

    rt.momentary_hold = false;
    rt.seq_active = true;
    rt.total_presses = constrain(count, (uint8_t)1, (uint8_t)5);
    rt.presses_left = rt.total_presses;
    rt.in_press = true;
    rt.stage_deadline = millis() + length_ms;
    _writeLevel(index, true);
}

void RelayManager::stopSequence(uint8_t index) {
    if (index >= g_config.relay_count || index >= BANK_CAPACITY) return;
    FxRelayRuntime& rt = _rt[index];
    rt.seq_active = false;
    rt.presses_left = 0;
    rt.momentary_hold = false;
    _writeLevel(index, false);
}

void RelayManager::_sequencerTick(uint8_t i) {
    FxRelayRuntime& rt = _rt[i];
    if (!rt.seq_active || rt.momentary_hold) return;

    uint32_t now = millis();
    if ((int32_t)(now - rt.stage_deadline) < 0) return;

    if (rt.in_press) {
        _writeLevel(i, false);
        rt.presses_left--;
        if (rt.presses_left > 0) {
            rt.in_press = false;
            rt.stage_deadline = now + g_config.fx[i].press_gap_ms;
        } else {
            rt.seq_active = false;   // Complete
        }
    } else {
        rt.in_press = true;
        rt.stage_deadline = now + g_config.fx[i].press_length_ms;
        _writeLevel(i, true);
    }
}

/* ============================================================================
 * CLOCK MODE - periodic re-fire
 * ============================================================================ */

void RelayManager::_clockTick(uint8_t i) {
    FxRelaySettings& s = g_config.fx[i];
    if (!s.clock_enable || !s.enabled) return;

    uint32_t now = millis();
    // Fire only when idle so we never truncate a running sequence
    if (!_rt[i].seq_active &&
        (now - _rt[i].clock_last_fire) >= s.clock_interval_ms) {
        _rt[i].clock_last_fire = now;
        fireSequence(i);
        log_d("Relay %u clock fire (every %ums)", i, s.clock_interval_ms);
    }
}

/* ============================================================================
 * MOMENTARY HOLD
 * ============================================================================ */

void RelayManager::startHold(uint8_t index) {
    if (index >= g_config.relay_count || index >= BANK_CAPACITY || !g_config.fx[index].enabled) return;
    FxRelayRuntime& rt = _rt[index];
    rt.seq_active = false;
    rt.presses_left = 0;
    rt.momentary_hold = true;
    _writeLevel(index, true);
}

void RelayManager::endHold(uint8_t index) {
    if (index >= g_config.relay_count || index >= BANK_CAPACITY) return;
    FxRelayRuntime& rt = _rt[index];
    if (rt.momentary_hold) {
        rt.momentary_hold = false;
        _writeLevel(index, false);
    }
}

/* ============================================================================
 * DIRECT CONTROL
 * ============================================================================ */

void RelayManager::setPressed(uint8_t index, bool pressed) {
    if (index >= g_config.relay_count || index >= BANK_CAPACITY) return;
    stopSequence(index);
    _writeLevel(index, pressed);
}

void RelayManager::setAllReleased() {
    for (uint8_t i = 0; i < g_config.relay_count && i < BANK_CAPACITY; i++) stopSequence(i);
}

bool RelayManager::_cooldownOk(uint8_t index) const {
    if (index >= BANK_CAPACITY) return false;
    return (millis() - _last_breath_fire[index]) >= g_config.auto_trigger_cooldown_ms;
}

/* ============================================================================
 * AUTOMATIC TRIGGERS - layer-linked + breath events
 * ============================================================================ */

void RelayManager::_autoTriggerCheck(bool state_changed, ApparatusState_t prev,
                                     ApparatusState_t new_state,
                                     float agc, float velocity_cm_s) {
    uint32_t now = millis();

    // --- Layer/state-linked triggers ---
    if (state_changed) {
        for (uint8_t i = 0; i < g_config.relay_count && i < BANK_CAPACITY; i++) {
            FxRelaySettings& s = g_config.fx[i];
            if (!s.enabled) continue;
            bool fire = false;
            switch (s.trigger) {
                case TRIG_ON_L3_CUT:      fire = (new_state == STATE_CONTACT); break;
                case TRIG_ON_L3_RELEASE:  fire = (prev != STATE_CONTACT) &&
                                                 (new_state != STATE_CONTACT); break;
                case TRIG_ON_L2_ENTRY:    fire = (new_state == STATE_MACRO);   break;
                case TRIG_ON_BREATH_LOCK: fire = (new_state == STATE_MICRO);   break;
                case TRIG_ON_L1_RETURN:   fire = (new_state == STATE_IDLE);    break;
                default: break;
            }
            if (fire && _cooldownOk(i)) {
                fireSequence(i);
                _last_breath_fire[i] = now;
                log_i("Auto-fire R%u (%s) on state %s", i, s.name,
                      STATE_NAMES[new_state]);
            }
        }
    }

    // --- Breath peak triggers (edge-latched on AGC wave crossing) ---
    static float s_prev_agc = 0.0f;
    float thr = g_config.breath_threshold;

    bool inhale_cross = (s_prev_agc < thr) && (agc >= thr);
    bool exhale_cross = (s_prev_agc > -thr) && (agc <= -thr);
    s_prev_agc = agc;

    if (inhale_cross || exhale_cross) {
        uint8_t want = inhale_cross ? TRIG_INHALE : TRIG_EXHALE;
        for (uint8_t i = 0; i < g_config.relay_count && i < BANK_CAPACITY; i++) {
            if (g_config.fx[i].enabled && g_config.fx[i].trigger == want &&
                _cooldownOk(i)) {
                fireSequence(i);
                _last_breath_fire[i] = now;
            }
        }
    }

    // --- Velocity-based Directional Triggers ---
    if (velocity_cm_s < g_config.lunge_threshold_cm_s) {
        if (!_lunge_latched) {
            _lunge_latched = true;
            for (uint8_t i = 0; i < g_config.relay_count && i < BANK_CAPACITY; i++) {
                if (g_config.fx[i].enabled && g_config.fx[i].trigger == TRIG_ON_LUNGE && _cooldownOk(i)) {
                    fireSequence(i);
                    _last_breath_fire[i] = now;
                }
            }
            log_i("Velocity < %.1f cm/s: LUNGE TRIGGER", g_config.lunge_threshold_cm_s);
        }
    } else if (velocity_cm_s > g_config.lunge_threshold_cm_s * 0.5f) { // Hysteresis to unlatch
        _lunge_latched = false;
    }

    if (velocity_cm_s > g_config.retreat_threshold_cm_s) {
        if (!_retreat_latched) {
            _retreat_latched = true;
            for (uint8_t i = 0; i < g_config.relay_count && i < BANK_CAPACITY; i++) {
                if (g_config.fx[i].enabled && g_config.fx[i].trigger == TRIG_ON_RETREAT && _cooldownOk(i)) {
                    fireSequence(i);
                    _last_breath_fire[i] = now;
                }
            }
            log_i("Velocity > %.1f cm/s: RETREAT TRIGGER", g_config.retreat_threshold_cm_s);
        }
    } else if (velocity_cm_s < g_config.retreat_threshold_cm_s * 0.5f) {
        _retreat_latched = false;
    }

    // --- Velocity Zero-Crossing Trigger (Micro-movements) ---
    // Only process zero-crosses if we are relatively stationary (e.g., Micro state)
    if (new_state == STATE_MICRO) {
        bool crossed_zero = (_last_velocity < 0.0f && velocity_cm_s >= 0.0f) ||
                            (_last_velocity > 0.0f && velocity_cm_s <= 0.0f);
        
        // Ensure it's a "real" crossing and not just tiny floating point noise (hysteresis)
        if (crossed_zero && fabsf(velocity_cm_s - _last_velocity) > 0.5f) {
            if ((now - _last_zero_fire) > 300) { // Fast 300ms throttle for zero crossings
                for (uint8_t i = 0; i < g_config.relay_count && i < BANK_CAPACITY; i++) {
                    if (g_config.fx[i].enabled && g_config.fx[i].trigger == TRIG_VELOCITY_ZERO) {
                        fireSequence(i);
                        // Deliberately bypassing _cooldownOk here to allow rapid firing
                    }
                }
                _last_zero_fire = now;
            }
        }
    }

    _last_velocity = velocity_cm_s;
}

/* ============================================================================
 * INTERNALS
 * ============================================================================ */

void RelayManager::_writeLevel(uint8_t index, bool pressed) {
    if (index >= g_config.relay_count || index >= BANK_CAPACITY) return;
    _rt[index].level = pressed;
#ifndef APPARATUS_NATIVE_TEST
    int pin = g_config.layout.relay_pins[index];
    if (pin >= 0) {
        digitalWrite(pin, pressed ? LOW : HIGH);  // Active-LOW board
    }
#endif
}

uint16_t RelayManager::getPattern() const {
    uint16_t p = 0;
    for (uint8_t i = 0; i < g_config.relay_count && i < BANK_CAPACITY; i++) {
        if (_rt[i].level) p |= (1 << i);
    }
    return p;
}

void RelayManager::printStatus() const {
    log_i("Relays pressed=0x%04X seq:", getPattern());
    for (uint8_t i = 0; i < g_config.relay_count && i < BANK_CAPACITY; i++) {
        if (_rt[i].seq_active) {
            log_i("  [%d] %s: %u/%u presses left", i, g_config.fx[i].name,
                  _rt[i].presses_left, _rt[i].total_presses);
        }
    }
}

/* ============================================================================
 * BOOT SEQUENCER - mixer power-on ritual
 * ============================================================================ */

void BootSequencer::start() {
    if (!g_config.boot.enabled || g_config.boot.step_count == 0) return;
    _active = true;
    _current_step = 0;
    _presses_done = 0;
    _await_gap_or_dwell = false;
    _deadline = millis() + g_config.boot.start_delay_ms;
    log_i("BootSequencer armed: %u steps after %ums delay",
          g_config.boot.step_count, g_config.boot.start_delay_ms);
}

void BootSequencer::update(RelayManager& relays) {
    if (!_active) return;
    uint32_t now = millis();

    // Wait out the start delay / inter-press gap / post-step dwell
    if ((int32_t)(now - _deadline) < 0) return;

    // Don't stomp on a relay that something else is driving
    BootStep& step = g_config.boot.steps[_current_step];

    if (!_await_gap_or_dwell) {
        // Press down
        relays.setPressed(step.relay, true);
        _deadline = now + step.length_ms;
        _await_gap_or_dwell = true;
    } else {
        // Release up
        relays.setPressed(step.relay, false);
        _presses_done++;

        if (_presses_done < constrain(step.presses, (uint8_t)1, (uint8_t)5)) {
            // More presses in this step -> gap then press again
            _deadline = now + step.gap_ms;
            _await_gap_or_dwell = false;
        } else if (++_current_step < g_config.boot.step_count) {
            // Next step after dwell
            _presses_done = 0;
            _await_gap_or_dwell = false;
            _deadline = now + constrain(step.wait_after_ms, (uint16_t)50, (uint16_t)60000);
        } else {
            _active = false;
            log_i("BootSequencer complete");
        }
    }
}

/* ============================================================================
 * BUTTON BANK - tap / multi-click / long-press
 * ============================================================================ */

void ButtonBank::begin() {
    for (uint8_t i = 0; i < BUTTON_MAX; i++) {
        pinMode(BUTTON_PINS[i], INPUT);  // Input-only pins need external pull-downs
    }
    log_i("ButtonBank: %d buttons wired of %d supported",
          BUTTON_MAX, BUTTON_MAX);
}

void ButtonBank::update(RelayManager& relays) {
    uint32_t now = millis();
    uint32_t debounce = g_config.button_debounce_ms;
    uint32_t click_win = g_config.multiclick_window_ms;
    uint32_t long_ms = g_config.long_press_ms;

    for (uint8_t i = 0; i < BUTTON_MAX; i++) {
        bool raw = (digitalRead(BUTTON_PINS[i]) == BUTTON_ACTIVE_LEVEL);
        BtnState& b = _st[i];

        if (raw != b.last_raw) {
            b.last_change_ms = now;
            b.last_raw = raw;
        }
        if ((now - b.last_change_ms) >= debounce && raw != b.stable) {
            b.stable = raw;
        }

        if (b.stable && !b.is_down) {
            b.is_down = true;
            b.down_since_ms = now;
            b.long_fired = false;
        } else if (!b.stable && b.is_down) {
            b.is_down = false;
            if (!b.long_fired) {
                if (b.click_count == 0) b.first_click_ms = now;
                b.click_count++;
            }
        }

        if (b.is_down && !b.long_fired && (now - b.down_since_ms) >= long_ms) {
            b.long_fired = true;
            b.click_count = 0;
            relays.startHold(i);
        }

        if (!b.is_down && b.click_count > 0 &&
            (now - b.first_click_ms) >= click_win) {
            _handleTap(i, b.click_count, relays);
            b.click_count = 0;
        }
    }
}

void ButtonBank::_handleTap(uint8_t i, uint8_t clicks, RelayManager& fx) {
    // 1 click  -> configured sequence
    // N clicks -> single extended press (N x configured length, capped 2 s)
    FxRelaySettings& s = g_config.fx[i];
    if (clicks == 1) {
        fx.fireSequence(i);
    } else {
        uint8_t mult = constrain(clicks, (uint8_t)1, (uint8_t)3);
        fx.fireSequenceCustom(i,
                              (uint16_t)constrain((uint32_t)s.press_length_ms * mult,
                                                  (uint32_t)50, (uint32_t)2000),
                              1, s.press_gap_ms);
    }
    log_d("BTN%d x%d fired", i, clicks);
}