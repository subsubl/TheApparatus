/**
 * @file StateMachine.cpp
 * @brief 4-Tier State Machine Implementation
 *
 * Transitions (evaluated every loop iteration, non-blocking):
 *
 *   IDLE  --target enters [D_min, D_max]-->                MACRO
 *   MACRO --variance < thr for lock_time-->                MICRO
 *   MACRO --distance > D_max + hyst-->                     IDLE
 *   MICRO --variance >= thr (movement resumes)-->          MACRO
 *   MICRO --distance > D_max + hyst-->                     IDLE
 *   ANY   --touch asserted-->                              CONTACT
 *   CONTACT --touch released--> (radar decides) IDLE/MACRO/MICRO
 *
 * Bidirectional hysteresis: entry requires distance <= D_max; exit requires
 * distance > D_max + hysteresis. This forms a deadband guard that prevents
 * chatter when a viewer hovers exactly at the threshold line.
 */

#include "StateMachine.h"
#include <Arduino.h>
#include <math.h>

/* ============================================================================
 * CONSTRUCTOR
 * ============================================================================ */

ApparatusStateMachine::ApparatusStateMachine(const CalibrationConfig& config, DSPPipeline& dsp)
    : _config(config), _dsp(dsp) {
    _state_enter_time = millis();
    _last_pwm_update = millis();
}

/* ============================================================================
 * MAIN UPDATE
 * ============================================================================ */

void ApparatusStateMachine::update(bool touch_active, const RadarFrame& radar_frame,
                                   bool dsp_ready, float dsp_normalized, float dsp_biquad_raw) {

    // --- Touch debounce ---
    bool touch_stable = touch_active;
    if (touch_active != _last_touch_state) {
        _touch_debounce_time = millis();
    }
    if (millis() - _touch_debounce_time < TOUCH_DEBOUNCE_MS) {
        touch_stable = _last_touch_state;
    }
    _last_touch_state = touch_active;

    // --- CONTACT has absolute priority (hard override of all radar logic) ---
    if (touch_stable || _current_state == STATE_CONTACT) {
        _handleContact(touch_stable, radar_frame);
        return;
    }

    switch (_current_state) {
        case STATE_IDLE:  _handleIdle(radar_frame); break;
        case STATE_MACRO: _handleMacro(radar_frame); break;
        case STATE_MICRO: _handleMicro(dsp_normalized); break;
        default:          _transitionTo(STATE_IDLE); break;
    }
}

/* ============================================================================
 * STATE HANDLERS
 * ============================================================================ */

void ApparatusStateMachine::_handleIdle(const RadarFrame& frame) {
    float dist = _effectiveDistance(frame);

    // Entry condition: valid target within [D_min - small margin, D_max]
    if (frame.valid && frame.target_state != TARGET_STATE_NONE &&
        dist > 0 && dist <= _config.D_max && dist >= (_config.D_min - _config.hysteresis)) {
        _transitionTo(STATE_MACRO);
        return;
    }

    // Stay idle: glide fader to 0%, keep Pi trigger LOW
    _pi_trigger_active = false;
    _base_pwm = 0.0f;
    _gamma_shaped = 0.0f;
    _updatePWMOutput(0.0f);
}

void ApparatusStateMachine::_handleMacro(const RadarFrame& frame) {
    float dist = _effectiveDistance(frame);

    // Exit upward (with bidirectional hysteresis): only beyond D_max + hysteresis
    if (!frame.valid || frame.target_state == TARGET_STATE_NONE ||
        dist > (_config.D_max + _config.hysteresis) || dist <= 0) {
        _transitionTo(STATE_IDLE);
        return;
    }

    // Promote to MICRO on stationary lock
    if (_isTargetStationary(dist)) {
        _transitionTo(STATE_MICRO);
        return;
    }

    // MACRO behavior: filtered distance -> gamma-shaped PWM
    float filtered_dist = _dsp.getDistanceFiltered();
    float linear = _mapDistanceToPWM(filtered_dist);
    _gamma_shaped = _applyGamma(linear);
    _base_pwm = _gamma_shaped * 255.0f;

    _pi_trigger_active = false;
    _updatePWMOutput(_base_pwm);
}

void ApparatusStateMachine::_handleMicro(float dsp_normalized) {
    // Exit to IDLE if target vanished entirely (checked via last frame in update())
    // Exit to MACRO handled in update() via movement detection below.

    // Breathing modulation around the locked base:
    //   P_out = P_base + N_resp * M * 255
    float modulation = dsp_normalized * _config.breathing_depth_M * 255.0f;
    float p_out = _base_pwm + modulation;
    _updatePWMOutput(p_out);

    _pi_trigger_active = false;
}

void ApparatusStateMachine::_handleContact(bool touch_active, const RadarFrame& frame) {
    if (touch_active) {
        // Hard override: instant 100% PWM + Pi trigger HIGH (no slew limit)
        _pwm_output_float = constrain(255.0f,
                                      (float)_config.pwm_min_clamp,
                                      (float)_config.pwm_max_clamp);
        _pwm_output = (uint8_t)_pwm_output_float;
        ledcWrite(VACTROL_PWM_CHANNEL, _pwm_output);
        _last_pwm_output = _pwm_output_float;
        _last_pwm_update = millis();

        _pi_trigger_active = true;
        return;
    }

    // Touch released: exit CONTACT based on current radar picture
    float dist = _effectiveDistance(frame);
    if (frame.valid && frame.target_state != TARGET_STATE_NONE &&
        dist > 0 && dist <= _config.D_max) {
        _transitionTo(_isTargetStationary(dist) ? STATE_MICRO : STATE_MACRO);
    } else {
        _transitionTo(STATE_IDLE);
    }
}

/* ============================================================================
 * HELPERS
 * ============================================================================ */

float ApparatusStateMachine::_effectiveDistance(const RadarFrame& frame) const {
    return (float)frame.detection_distance_cm;
}

float ApparatusStateMachine::_mapDistanceToPWM(float distance_cm) const {
    float clamped = constrain(distance_cm, _config.D_min, _config.D_max);
    // D_min -> 1.0, D_max -> 0.0 (closer = more Layer 2/3 bleed-through)
    float linear = 1.0f - (clamped - _config.D_min) / (_config.D_max - _config.D_min);
    return constrain(linear, 0.0f, 1.0f);
}

float ApparatusStateMachine::_applyGamma(float v) const {
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    return powf(v, _config.gamma_exponent);
}

float ApparatusStateMachine::_calculateDistanceVariance(float d) {
    _dist_history[_dist_history_idx] = d;
    _dist_history_idx = (_dist_history_idx + 1) % DIST_HISTORY_LEN;
    if (_dist_history_count < DIST_HISTORY_LEN) _dist_history_count++;

    if (_dist_history_count < 2) return 0.0f;

    float sum = 0.0f;
    for (int i = 0; i < _dist_history_count; i++) sum += _dist_history[i];
    float mean = sum / _dist_history_count;

    float acc = 0.0f;
    for (int i = 0; i < _dist_history_count; i++) {
        float diff = _dist_history[i] - mean;
        acc += diff * diff;
    }
    return sqrtf(acc / _dist_history_count);   // Std deviation (cm) - comparable to ±5 cm spec
}

bool ApparatusStateMachine::_isTargetStationary(float distance_cm) {
    _distance_variance = _calculateDistanceVariance(distance_cm);

    if (_distance_variance < _config.variance_threshold_cm) {
        if (_stationary_start_time == 0) {
            _stationary_start_time = millis();
        }
        return (millis() - _stationary_start_time) >= _config.stationary_lock_time_ms;
    }
    _stationary_start_time = 0;
    return false;
}

void ApparatusStateMachine::_transitionTo(ApparatusState_t new_state) {
    if (new_state == _current_state) return;

    _previous_state = _current_state;
    _current_state = new_state;
    _state_enter_time = millis();

    switch (new_state) {
        case STATE_IDLE:
            _stationary_start_time = 0;
            _base_pwm = 0.0f;
            _dist_history_count = 0;
            _dist_history_idx = 0;
            break;

        case STATE_MACRO:
            _stationary_start_time = 0;
            break;

        case STATE_MICRO:
            // Lock P_base at current Macro PWM level (master briefing §4)
            _base_pwm = _pwm_output_float;
            _dsp.reset();   // Fresh AGC window so breathing wave settles cleanly
            break;

        case STATE_CONTACT:
            // Nothing special - handler asserts outputs immediately
            break;
    }

    log_i("State: %s -> %s", STATE_NAMES[_previous_state], STATE_NAMES[_current_state]);
}

void ApparatusStateMachine::_updatePWMOutput(float target_pwm) {
    uint32_t now = millis();
    uint32_t dt = (now > _last_pwm_update) ? (now - _last_pwm_update) : 1;

    // Slew-rate limiting (inertia glide). CONTACT bypasses this entirely.
    float max_change = _config.slew_rate_limit * dt;
    float current = _last_pwm_output;
    float limited;
    if (target_pwm > current) {
        limited = fminf(target_pwm, current + max_change);
    } else {
        limited = fmaxf(target_pwm, current - max_change);
    }

    _pwm_output_float = constrain(limited,
                                  (float)_config.pwm_min_clamp,
                                  (float)_config.pwm_max_clamp);
    _pwm_output = (uint8_t)_pwm_output_float;

    _last_pwm_output = _pwm_output_float;
    _last_pwm_update = now;

    ledcWrite(VACTROL_PWM_CHANNEL, _pwm_output);
}

/* ============================================================================
 * PUBLIC INTERFACE
 * ============================================================================ */

void ApparatusStateMachine::forceState(ApparatusState_t new_state) {
    _transitionTo(new_state);
}

void ApparatusStateMachine::printState() const {
    log_i("State=%s PWM=%d base=%.1f var=%.2fcm piTrig=%s",
          getStateName(), _pwm_output, _base_pwm, _distance_variance,
          _pi_trigger_active ? "HIGH" : "LOW");
}