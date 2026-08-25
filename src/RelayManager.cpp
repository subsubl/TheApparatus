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
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        _active_pins[i] = RELAY_DEFS[i].pin;
        pinMode(_active_pins[i], OUTPUT);
        digitalWrite(_active_pins[i], RELAY_BOOT_SAFE_LEVEL);
        _rt[i] = FxRelayRuntime{};
    }
    log_i("RelayManager: %d channels (active-LOW, released)", RELAY_COUNT);
}

/* ============================================================================
 * GPIO REMAPPING (GUI)
 * ============================================================================ */

bool RelayManager::remapPin(uint8_t index, uint8_t new_pin) {
    if (index >= RELAY_COUNT) return false;

    // Reject conflicts with other relays and critical fixed pins
    static const uint8_t forbidden[] = {0, 1, 3, 6, 7, 8, 9, 10, 11,
                                        RADAR_RX_PIN, RADAR_TX_PIN,
                                        PI_LINK_TX_PIN};
    for (uint8_t f = 0; f < sizeof(forbidden); f++) {
        if (new_pin == forbidden[f]) {
            log_w("Remap refused: GPIO %u protected", new_pin);
            return false;
        }
    }
    for (uint8_t j = 0; j < RELAY_COUNT; j++) {
        if (j != index && _active_pins[j] == new_pin) {
            log_w("Remap refused: GPIO %u used by relay %u", new_pin, j);
            return false;
        }
    }

    // Release old pin to safe level, adopt new
    stopSequence(index);
    digitalWrite(_active_pins[index], RELAY_BOOT_SAFE_LEVEL);
    _active_pins[index] = new_pin;
    pinMode(new_pin, OUTPUT);
    digitalWrite(new_pin, RELAY_BOOT_SAFE_LEVEL);
    log_i("Relay %u remapped to GPIO %u", index, new_pin);
    return true;
}

/* ============================================================================
 * UPDATE
 * ============================================================================ */

void RelayManager::update(bool state_changed, ApparatusState_t new_state, float agc) {
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        _sequencerTick(i);
        _clockTick(i);
    }
    _autoTriggerCheck(state_changed, new_state, agc);
}

/* ============================================================================
 * SEQUENCED FIRING
 * ============================================================================ */

void RelayManager::fireSequence(uint8_t index) {
    if (index >= RELAY_COUNT || !g_config.fx[index].enabled) return;
    FxRelaySettings& s = g_config.fx[index];
    fireSequenceCustom(index, s.press_length_ms, s.press_count, s.press_gap_ms);
}

void RelayManager::fireSequenceCustom(uint8_t index, uint16_t length_ms,
                                      uint8_t count, uint16_t gap_ms) {
    if (index >= RELAY_COUNT || !g_config.fx[index].enabled) return;
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
    if (index >= RELAY_COUNT) return;
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
    if (index >= RELAY_COUNT || !g_config.fx[index].enabled) return;
    FxRelayRuntime& rt = _rt[index];
    rt.seq_active = false;
    rt.presses_left = 0;
    rt.momentary_hold = true;
    _writeLevel(index, true);
}

void RelayManager::endHold(uint8_t index) {
    if (index >= RELAY_COUNT) return;
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
    if (index >= RELAY_COUNT) return;
    stopSequence(index);
    _writeLevel(index, pressed);
}

void RelayManager::setAllReleased() {
    for (uint8_t i = 0; i < RELAY_COUNT; i++) stopSequence(i);
}

bool RelayManager::_cooldownOk(uint8_t index) const {
    return (millis() - _last_breath_fire[index]) >= g_config.auto_trigger_cooldown_ms;
}

/* ============================================================================
 * AUTOMATIC TRIGGERS - layer-linked + breath events
 * ============================================================================ */

void RelayManager::_autoTriggerCheck(bool state_changed, ApparatusState_t new_state,
                                     float agc) {
    uint32_t now = millis();

    // --- Layer/state-linked triggers ---
    if (state_changed) {
        for (uint8_t i = 0; i < RELAY_COUNT; i++) {
            FxRelaySettings& s = g_config.fx[i];
            if (!s.enabled) continue;
            bool fire = false;
            switch (s.trigger) {
                case TRIG_ON_L3_CUT:      fire = (new_state == STATE_CONTACT); break;
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
        for (uint8_t i = 0; i < RELAY_COUNT; i++) {
            if (g_config.fx[i].enabled && g_config.fx[i].trigger == want &&
                _cooldownOk(i)) {
                fireSequence(i);
                _last_breath_fire[i] = now;
            }
        }
    }
}

/* ============================================================================
 * INTERNALS
 * ============================================================================ */

void RelayManager::_writeLevel(uint8_t index, bool pressed) {
    if (index >= RELAY_COUNT) return;
    _rt[index].level = pressed;
    digitalWrite(_active_pins[index], pressed ? LOW : HIGH);  // Active-LOW board
}

uint8_t RelayManager::getPattern() const {
    uint8_t p = 0;
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        if (_rt[i].level) p |= (1 << i);
    }
    return p;
}

void RelayManager::printStatus() const {
    log_i("Relays pressed=0x%02X seq:", getPattern());
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
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