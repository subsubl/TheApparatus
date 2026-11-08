/**
 * @file VactrolManager.h
 * @brief 6-channel LEDC PWM engine for WJ-AVE5 slider vactrols
 *
 * Each channel drives an LED-LDR vactrol bridging one mixer slider wiper.
 * Features:
 *  - AUTO mode: channel target set by algorithms (Mix: state machine;
 *    others may later be bound to radar/DSP features)
 *  - MANUAL mode: fixed slider value from GUI
 *  - Per-channel min/max clamps and slew-rate limiting (inertia glide)
 *  - Gamma-corrected output to linearize LDR response
 *
 * All updates are non-blocking; call update() every loop iteration.
 */

#pragma once

#include "PinDefinitions.h"

class VactrolManager {
public:
    void begin();

    // Call every loop iteration.
    void update(float mix_auto_target_0to1, float velocity_cm_s, float breath_wave, bool contact_active);

    void setManual(uint8_t ch, uint16_t value_0_1023);
    void setAutoMode(uint8_t ch, bool auto_mode);

    uint16_t getValue(uint8_t ch) const { return (ch < BANK_CAPACITY) ? _current[ch] : 0; }
    float getTargetFloat(uint8_t ch) const { return (ch < BANK_CAPACITY) ? _target_f[ch] : 0.0f; }

    // Dynamic channel management
    bool addVactrol(int pin = -1);
    bool removeLastVactrol();
    bool remapPin(uint8_t ch, int new_pin);
    int getPin(uint8_t ch) const {
        return (ch < g_config.vactrol_count) ? g_config.layout.vactrol_pins[ch] : -1;
    }
    uint8_t getChannelCount() const { return g_config.vactrol_count; }

private:
    uint16_t _current[BANK_CAPACITY] = {0};
    float    _target_f[BANK_CAPACITY] = {0};
    uint32_t _last_update_ms = 0;

    void _writeChannel(uint8_t ch, uint16_t counts);
    uint16_t _applyGamma(uint8_t ch, float linear01) const;
};