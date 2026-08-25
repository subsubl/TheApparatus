/**
 * @file StateMachine.cpp
 * @brief 4-Tier State Machine Implementation (serial Pi link edition)
 *
 * Transitions:
 *   IDLE  --target enters [D_min, D_max]-->          MACRO
 *   MACRO --variance < thr for lock_time-->          MICRO
 *   MACRO --distance > D_max + hyst-->               IDLE
 *   MICRO --variance >= thr-->                       MACRO
 *   MICRO --distance > D_max + hyst-->               IDLE
 *   ANY    --touch-->                                CONTACT
 *   CONTACT --touch released-->                      radar decides
 *
 * Mix output is normalized 0.0-1.0; the VactrolManager applies gamma,
 * clamps, and slew. Pi trigger: CONTACT sends TRIGGER_SEEK once per edge.
 */

#include "StateMachine.h"
#include <Arduino.h>
#include <math.h>

extern HardwareSerial PiLink;  // Defined in main.cpp

/* ============================================================================
 * CONSTRUCTOR
 * ============================================================================ */

ApparatusStateMachine::ApparatusStateMachine(DSPPipeline& dsp) : _dsp(dsp) {
    _state_enter_time = millis();
}

/* ============================================================================
 * PI SERIAL LINK HELPERS
 * ============================================================================ */

bool ApparatusStateMachine::getPiTrigger() const { return _pi_trigger_active; }

static void piSend(const char* cmd) {
    PiLink.print(cmd);
    PiLink.print('\n');
    log_i("PiLink -> %s", cmd);
}

/* ============================================================================
 * MAIN UPDATE
 * ============================================================================ */

void ApparatusStateMachine::update(bool touch_active, const RadarFrame& radar_frame,
                                   bool dsp_ready, float dsp_normalized,
                                   float dsp_biquad_raw) {

    // Touch debounce
    bool touch_stable = touch_active;
    if (touch_active != _last_touch_state) {
        _touch_debounce_time = millis();
    }
    if (millis() - _touch_debounce_time < TOUCH_DEBOUNCE_MS) {
        touch_stable = _last_touch_state;
    }
    _last_touch_state = touch_active;

    // CONTACT has absolute priority
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

    if (frame.valid && frame.target_state != TARGET_STATE_NONE &&
        dist > 0 && dist <= g_config.D_max &&
        dist >= (g_config.D_min - g_config.hysteresis)) {
        _transitionTo(STATE_MACRO);
        return;
    }

    _pi_trigger_active = false;
    _base_pwm = 0.0f;
    _gamma_shaped = 0.0f;

    // Distance-zone Pi automation: far zone keeps Layer 2 loop
    if (_pi_loop_b_set && frame.valid &&
        dist > g_config.pi_zone_far_cm) {
        piSend("LOOP_A");
        _pi_loop_b_set = false;
    }

    _updatePWMOutput(0.0f);
}

void ApparatusStateMachine::_handleMacro(const RadarFrame& frame) {
    float dist = _effectiveDistance(frame);

    if (!frame.valid || frame.target_state == TARGET_STATE_NONE ||
        dist > (g_config.D_max + g_config.hysteresis) || dist <= 0) {
        _transitionTo(STATE_IDLE);
        return;
    }

    if (_isTargetStationary(dist)) {
        _transitionTo(STATE_MICRO);
        return;
    }

    float filtered_dist = _dsp.getDistanceFiltered();
    float linear = _mapDistanceToPWM(filtered_dist);
    _gamma_shaped = powf(linear, g_config.gamma_exponent);
    _base_pwm = _gamma_shaped * 255.0f;

    _pi_trigger_active = false;

    // Near zone hint: viewer well inside range
    if (!_pi_loop_b_set && dist < g_config.pi_zone_near_cm) {
        piSend("LOOP_B");
        _pi_loop_b_set = true;
    } else if (_pi_loop_b_set && dist > g_config.pi_zone_far_cm) {
        piSend("LOOP_A");
        _pi_loop_b_set = false;
    }

    _updatePWMOutput(_gamma_shaped);
}

void ApparatusStateMachine::_handleMicro(float dsp_normalized) {
    float modulation = dsp_normalized * g_config.breathing_depth_M;
    float p_out = constrain(_pwm_output_float + modulation * 0.25f, 0.0f, 1.0f);
    _updatePWMOutput(p_out);

    _pi_trigger_active = false;
}

void ApparatusStateMachine::_handleContact(bool touch_active, const RadarFrame& frame) {
    if (touch_active) {
        if (!_pi_trigger_active) {
            // Rising edge of CONTACT: fire the Layer 3 cut ONCE.
            // Live-camera superimposition is handled on the mixer itself:
            // a relay with trigger "Layer 3 Cut (Contact)" presses the
            // AVE5's camera/key button (see RelayManager auto-triggers).
            piSend("TRIGGER_SEEK");
            _pi_trigger_active = true;
        }
        _updatePWMOutput(1.0f);   // Instant full-scale (VactrolManager bypasses slew)
        return;
    }

    // Touch released
    float dist = _effectiveDistance(frame);
    if (frame.valid && frame.target_state != TARGET_STATE_NONE &&
        dist > 0 && dist <= g_config.D_max) {
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
    float clamped = constrain(distance_cm, g_config.D_min, g_config.D_max);
    float linear = 1.0f - (clamped - g_config.D_min) / (g_config.D_max - g_config.D_min);
    return constrain(linear, 0.0f, 1.0f);
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
    return sqrtf(acc / _dist_history_count);
}

bool ApparatusStateMachine::_isTargetStationary(float distance_cm) {
    _distance_variance = _calculateDistanceVariance(distance_cm);

    if (_distance_variance < g_config.variance_threshold_cm) {
        if (_stationary_start_time == 0) {
            _stationary_start_time = millis();
        }
        return (millis() - _stationary_start_time) >= g_config.stationary_lock_time_ms;
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
            _base_pwm = _pwm_output_float * 255.0f;
            _dsp.reset();   // Fresh AGC window for clean breathing wave
            break;

        case STATE_CONTACT:
            break;
    }

    log_i("State: %s -> %s", STATE_NAMES[_previous_state], STATE_NAMES[_current_state]);
}

void ApparatusStateMachine::_updatePWMOutput(float target_norm) {
    uint32_t now = millis();
    uint32_t dt = (now > _last_pwm_update) ? (now - _last_pwm_update) : 1;
    _last_pwm_update = now;

    float max_change = g_config.slew_rate_limit * dt * 0.01f;  // Per-ms glide
    float current = _pwm_output_float;
    float limited;
    if (target_norm > current) {
        limited = fminf(target_norm, current + max_change);
    } else {
        limited = fmaxf(target_norm, current - max_change);
    }

    _pwm_output_float = constrain(limited, 0.0f, 1.0f);
    _pwm_output = (uint8_t)(_pwm_output_float * 255.0f + 0.5f);
}

void ApparatusStateMachine::forceState(ApparatusState_t new_state) {
    _transitionTo(new_state);
}

void ApparatusStateMachine::printState() const {
    log_i("State=%s mix=%.2f base=%.0f var=%.2fcm piTrig=%s",
          getStateName(), _pwm_output_float, _base_pwm, _distance_variance,
          _pi_trigger_active ? "HIGH" : "LOW");
}