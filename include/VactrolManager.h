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

#ifndef VACTROL_MANAGER_H
#define VACTROL_MANAGER_H

#include "PinDefinitions.h"

class VactrolManager {
public:
    void begin();

    // Call every loop iteration. mix_auto_target: algorithmic target for the
    // Mix/T-Bar channel in 0..1023 (already gamma-shaped by caller or raw -
    // manager applies its own gamma on top when cfg.gamma_exponent != 1).
    // contact_active: bypasses slew + clamps for instant full-scale T-Bar hit.
    void update(float mix_auto_target_0to1, bool contact_active);

    // Manual control
    void setManual(uint8_t ch, uint16_t value_0_1023);
    void setAutoMode(uint8_t ch, bool auto_mode);

    uint16_t getValue(uint8_t ch) const { return _current[ch]; }
    float getTargetFloat(uint8_t ch) const { return _target_f[ch]; }

private:
    uint16_t _current[VACTROL_COUNT] = {0};
    float    _target_f[VACTROL_COUNT] = {0};
    uint32_t _last_update_ms = 0;

    void _writeChannel(uint8_t ch, uint16_t counts);
    uint16_t _applyGamma(uint8_t ch, float linear01) const;
};

#endif // VACTROL_MANAGER_H