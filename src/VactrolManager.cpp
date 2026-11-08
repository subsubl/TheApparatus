/**
 * @file VactrolManager.cpp
 * @brief Multi-channel vactrol PWM engine implementation
 */

#include "VactrolManager.h"
#ifndef APPARATUS_NATIVE_TEST
#include <Arduino.h>
#endif
#include <math.h>

void VactrolManager::begin() {
    for (uint8_t ch = 0; ch < g_config.vactrol_count && ch < BANK_CAPACITY; ch++) {
        int pin = g_config.layout.vactrol_pins[ch];
#ifndef APPARATUS_NATIVE_TEST
        if (pin >= 0) {
            ledcSetup(ch, VACTROL_PWM_FREQ, VACTROL_PWM_RESOLUTION);
            ledcAttachPin(pin, ch);
        }
#endif
        _current[ch] = 0;
        _target_f[ch] = 0.0f;
        _writeChannel(ch, 0);
    }
    _last_update_ms = millis();
    log_i("VactrolManager: %d channels @ %d Hz, %d-bit", g_config.vactrol_count,
          VACTROL_PWM_FREQ, VACTROL_PWM_RESOLUTION);
}

void VactrolManager::update(float mix_auto_target_0to1, float velocity_cm_s, float breath_wave, bool contact_active) {
    uint32_t now = millis();
    uint32_t dt = (now > _last_update_ms) ? (now - _last_update_ms) : 1;
    _last_update_ms = now;

    // We can map absolute velocity from 0..100 cm/s to 0..1
    float abs_vel = fabsf(velocity_cm_s);
    float vel_0to1 = constrain(abs_vel / 100.0f, 0.0f, 1.0f);
    
    // Breath wave is already -1 to +1 (AGC normalized). Map to 0..1
    float breath_0to1 = constrain((breath_wave + 1.0f) * 0.5f, 0.0f, 1.0f);

    // === Compute per-channel targets ===
    for (uint8_t ch = 0; ch < g_config.vactrol_count && ch < BANK_CAPACITY; ch++) {
        VactrolSettings& vs = g_config.vactrol[ch];

        if (ch == VACT_MIX && vs.source_mode == 1) {
            // Mix channel: radar/state-machine driven when in AUTO
            float t = contact_active ? 1.0f : constrain(mix_auto_target_0to1, 0.0f, 1.0f);
            _target_f[ch] = t;
        } else {
            switch (vs.source_mode) {
                case 0: // Manual
                    _target_f[ch] = (float)vs.manual_value / VACTROL_PWM_MAX;
                    break;
                case 1: // Distance (Mix target)
                    _target_f[ch] = constrain(mix_auto_target_0to1, 0.0f, 1.0f);
                    break;
                case 2: // Velocity (Speed)
                    _target_f[ch] = vel_0to1;
                    break;
                case 3: // Breath
                    _target_f[ch] = breath_0to1;
                    break;
                case 4: // Lunge
                    _target_f[ch] = (velocity_cm_s < g_config.lunge_threshold_cm_s) ? 1.0f : 0.0f;
                    break;
                default:
                    _target_f[ch] = 0.0f;
                    break;
            }
        }
    }

    // === Slew + clamp + write ===
    for (uint8_t ch = 0; ch < g_config.vactrol_count && ch < BANK_CAPACITY; ch++) {
        VactrolSettings& vs = g_config.vactrol[ch];

        float target_counts_f = _applyGamma(ch, _target_f[ch]) * VACTROL_PWM_MAX;

        // Clamp window: map min/max clamps onto full-scale target
        float span = (float)(vs.max_clamp - vs.min_clamp);
        target_counts_f = vs.min_clamp + (target_counts_f / VACTROL_PWM_MAX) * span;

        float current_f = (float)_current[ch];
        float limited;

        if (ch == VACT_MIX && contact_active && vs.source_mode == 1) {
            // CONTACT hard override: instant snap, no slew
            limited = target_counts_f;
        } else {
            float active_slew = vs.slew_per_ms;
            if (vs.dynamic_slew) {
                // Slew up to 11x faster based on absolute velocity
                active_slew *= (1.0f + abs_vel / 10.0f);
            }
            float max_change = active_slew * dt;
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
    if (ch >= g_config.vactrol_count || ch >= BANK_CAPACITY) return;
    g_config.vactrol[ch].manual_value =
        constrain(value_0_1023, (uint16_t)g_config.vactrol[ch].min_clamp,
                 (uint16_t)g_config.vactrol[ch].max_clamp);
}

void VactrolManager::setAutoMode(uint8_t ch, bool auto_mode) {
    if (ch >= g_config.vactrol_count || ch >= BANK_CAPACITY) return;
    g_config.vactrol[ch].source_mode = auto_mode ? 1 : 0;
}

void VactrolManager::_writeChannel(uint8_t ch, uint16_t counts) {
#ifndef APPARATUS_NATIVE_TEST
    if (ch < BANK_CAPACITY && g_config.layout.vactrol_pins[ch] >= 0) {
        ledcWrite(ch, counts);
    }
#endif
}

// Gamma correction applied on the normalized target before scaling.
uint16_t VactrolManager::_applyGamma(uint8_t ch, float linear01) const {
    linear01 = constrain(linear01, 0.0f, 1.0f);
    float g = (ch < BANK_CAPACITY) ? g_config.vactrol[ch].gamma : 1.0f;
    if (g == 1.0f) return (uint16_t)(linear01 * VACTROL_PWM_MAX + 0.5f);
    float shaped = powf(linear01, g);
    return (uint16_t)(shaped * VACTROL_PWM_MAX + 0.5f);
}

bool VactrolManager::addVactrol(int pin) {
    AllocationResult ar = (pin < 0) ? apparatus::addOutput(g_config.layout, Bank::Vactrol)
                                    : apparatus::addOutput(g_config.layout, Bank::Vactrol, pin);
    if (!ar.ok) {
        log_w("addVactrol failed: %s", ar.error);
        return false;
    }
    g_config.vactrol_count = g_config.layout.vactrol_count;
    uint8_t ch = g_config.vactrol_count - 1;
    g_config.vactrol[ch] = VactrolSettings{};
    int new_pin = g_config.layout.vactrol_pins[ch];
#ifndef APPARATUS_NATIVE_TEST
    if (new_pin >= 0) {
        ledcSetup(ch, VACTROL_PWM_FREQ, VACTROL_PWM_RESOLUTION);
        ledcAttachPin(new_pin, ch);
        ledcWrite(ch, 0);
    }
#endif
    _current[ch] = 0;
    _target_f[ch] = 0.0f;
    log_i("Vactrol channel %u added on GPIO %d (total %d)", ch, new_pin, g_config.vactrol_count);
    return true;
}

bool VactrolManager::removeLastVactrol() {
    if (g_config.vactrol_count <= 0) return false;
    uint8_t ch = g_config.vactrol_count - 1;
    int old_pin = g_config.layout.vactrol_pins[ch];
    AllocationResult ar = apparatus::removeLastOutput(g_config.layout, Bank::Vactrol);
    if (!ar.ok) {
        log_w("removeLastVactrol failed: %s", ar.error);
        return false;
    }
#ifndef APPARATUS_NATIVE_TEST
    if (old_pin >= 0) {
        ledcDetachPin(old_pin);
        pinMode(old_pin, INPUT);
    }
#endif
    _current[ch] = 0;
    _target_f[ch] = 0.0f;
    g_config.vactrol[ch] = VactrolSettings{};
    g_config.vactrol_count = g_config.layout.vactrol_count;
    log_i("Vactrol channel %u removed from GPIO %d (total %d)", ch, old_pin, g_config.vactrol_count);
    return true;
}

bool VactrolManager::remapPin(uint8_t ch, int new_pin) {
    if (ch >= g_config.vactrol_count || ch >= BANK_CAPACITY) return false;
    int old_pin = g_config.layout.vactrol_pins[ch];
    AllocationResult ar = apparatus::remapOutput(g_config.layout, Bank::Vactrol, ch, new_pin);
    if (!ar.ok) {
        log_w("remapPin vactrol %u -> GPIO %d failed: %s", ch, new_pin, ar.error);
        return false;
    }
#ifndef APPARATUS_NATIVE_TEST
    if (old_pin >= 0) {
        ledcDetachPin(old_pin);
        pinMode(old_pin, INPUT);
    }
    if (new_pin >= 0) {
        ledcSetup(ch, VACTROL_PWM_FREQ, VACTROL_PWM_RESOLUTION);
        ledcAttachPin(new_pin, ch);
        ledcWrite(ch, _current[ch]);
    }
#endif
    log_i("Vactrol channel %u remapped: GPIO %d -> GPIO %d", ch, old_pin, new_pin);
    return true;
}