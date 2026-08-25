/**
 * @file VactrolManager.cpp
 * @brief Multi-channel vactrol PWM engine implementation
 */

#include "VactrolManager.h"
#include <Arduino.h>
#include <math.h>

void VactrolManager::begin() {
    for (uint8_t ch = 0; ch < VACTROL_COUNT; ch++) {
        ledcSetup(VACTROL_DEFS[ch].ledc_ch, VACTROL_PWM_FREQ, VACTROL_PWM_RESOLUTION);
        ledcAttachPin(VACTROL_DEFS[ch].pin, VACTROL_DEFS[ch].ledc_ch);
        _current[ch] = 0;
        _target_f[ch] = 0.0f;
        _writeChannel(ch, 0);
    }
    _last_update_ms = millis();
    log_i("VactrolManager: %d channels @ %d Hz, %d-bit", VACTROL_COUNT,
          VACTROL_PWM_FREQ, VACTROL_PWM_RESOLUTION);
}

void VactrolManager::update(float mix_auto_target_0to1, bool contact_active) {
    uint32_t now = millis();
    uint32_t dt = (now > _last_update_ms) ? (now - _last_update_ms) : 1;
    _last_update_ms = now;

    // === Compute per-channel targets ===
    for (uint8_t ch = 0; ch < VACTROL_COUNT; ch++) {
        VactrolSettings& vs = g_config.vactrol[ch];

        if (ch == VACT_MIX) {
            // Mix channel: radar/state-machine driven when in AUTO
            float t = contact_active ? 1.0f : constrain(mix_auto_target_0to1, 0.0f, 1.0f);
            _target_f[ch] = t;
        } else {
            _target_f[ch] = vs.auto_mode ? _target_f[ch]
                                         : (float)vs.manual_value / VACTROL_PWM_MAX;
        }
    }

    // === Slew + clamp + write ===
    for (uint8_t ch = 0; ch < VACTROL_COUNT; ch++) {
        VactrolSettings& vs = g_config.vactrol[ch];

        float target_counts_f = _applyGamma(ch, _target_f[ch]) * VACTROL_PWM_MAX;

        // Clamp window: map min/max clamps onto full-scale target
        float span = (float)(vs.max_clamp - vs.min_clamp);
        target_counts_f = vs.min_clamp + (target_counts_f / VACTROL_PWM_MAX) * span;

        float current_f = (float)_current[ch];
        float limited;

        if (ch == VACT_MIX && contact_active) {
            // CONTACT hard override: instant snap, no slew
            limited = target_counts_f;
        } else {
            float max_change = vs.slew_per_ms * dt;
            if (target_counts_f > current_f) {
                limited = fminf(target_counts_f, current_f + max_change);
            } else {
                limited = fmaxf(target_counts_f, current_f - max_change);
            }
        }

        uint16_t out = (uint16_t)constrain(limited, (float)vs.min_clamp,
                                           (float)vs.max_clamp);
        if (out != _current[ch]) {
            _current[ch] = out;
            _writeChannel(ch, out);
        }
    }
}

void VactrolManager::setManual(uint8_t ch, uint16_t value_0_1023) {
    if (ch >= VACTROL_COUNT) return;
    g_config.vactrol[ch].manual_value =
        constrain(value_0_1023, (uint16_t)g_config.vactrol[ch].min_clamp,
                 (uint16_t)g_config.vactrol[ch].max_clamp);
}

void VactrolManager::setAutoMode(uint8_t ch, bool auto_mode) {
    if (ch >= VACTROL_COUNT) return;
    g_config.vactrol[ch].auto_mode = auto_mode;
}

void VactrolManager::_writeChannel(uint8_t ch, uint16_t counts) {
    ledcWrite(VACTROL_DEFS[ch].ledc_ch, counts);
}

// Gamma correction applied on the normalized target before scaling.
// Uses the global crossfader gamma exponent (shared with Mix curve);
// per-channel gamma can be added later if the LDRs differ significantly.
uint16_t VactrolManager::_applyGamma(uint8_t ch, float linear01) const {
    linear01 = constrain(linear01, 0.0f, 1.0f);
    float g = g_config.gamma_exponent;
    if (g == 1.0f) return (uint16_t)(linear01 * VACTROL_PWM_MAX + 0.5f);
    float shaped = powf(linear01, g);
    return (uint16_t)(shaped * VACTROL_PWM_MAX + 0.5f);
}