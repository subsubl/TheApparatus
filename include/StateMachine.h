/**
 * @file StateMachine.h
 * @brief 4-Tier State Machine for The Apparatus
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "PinDefinitions.h"
#include "DSP.h"

class ApparatusStateMachine {
public:
    ApparatusStateMachine(DSPPipeline& dsp);

    // Main update - call every loop iteration
    void update(bool touch_active, const RadarFrame& radar_frame,
                bool dsp_ready, float dsp_normalized, float dsp_biquad_raw);

    // Getters
    ApparatusState_t getState() const { return _current_state; }
    const char* getStateName() const { return STATE_NAMES[_current_state]; }
    uint8_t getPWMOutput() const { return _pwm_output; }
    float getPWMOutputFloat() const { return _pwm_output_float; }
    bool getPiTrigger() const;
    float getBasePWM() const { return _base_pwm; }
    float getGammaShaped() const { return _gamma_shaped; }
    float getDistanceVariance() const { return _distance_variance; }

    void forceState(ApparatusState_t new_state);
    void printState() const;

private:
    DSPPipeline& _dsp;

    // State tracking
    ApparatusState_t _current_state = STATE_IDLE;
    ApparatusState_t _previous_state = STATE_IDLE;
    uint32_t _state_enter_time = 0;

    // Distance history ring buffer for variance
    static constexpr int DIST_HISTORY_LEN = 10;
    float _dist_history[DIST_HISTORY_LEN] = {0};
    int _dist_history_idx = 0;
    int _dist_history_count = 0;
    float _distance_variance = 0.0f;
    uint32_t _stationary_start_time = 0;

    // Output values (0.0-1.0 normalized mix level)
    uint8_t _pwm_output = 0;          // Legacy 0-255 mirror for telemetry
    float _pwm_output_float = 0.0f;   // Normalized mix target
    bool _pi_trigger_active = false;
    bool _pi_loop_b_set = false;      // Serial loop state tracking

    // MACRO/MICRO variables
    float _base_pwm = 0.0f;
    float _gamma_shaped = 0.0f;

    // Slew rate limiting
    uint32_t _last_pwm_update = 0;
    float _last_pwm_output = 0.0f;

    // Touch debouncing
    bool _last_touch_state = false;
    uint32_t _touch_debounce_time = 0;
    static constexpr uint32_t TOUCH_DEBOUNCE_MS = 50;

    // Handlers
    void _handleIdle(const RadarFrame& frame);
    void _handleMacro(const RadarFrame& frame);
    void _handleMicro(float dsp_normalized);
    void _handleContact(bool touch_active, const RadarFrame& frame);

    // Helpers
    float _effectiveDistance(const RadarFrame& frame) const;
    float _mapDistanceToPWM(float distance_cm) const;
    float _applyGamma(float v) const;
    float _calculateDistanceVariance(float d);
    bool _isTargetStationary(float distance_cm);
    void _transitionTo(ApparatusState_t new_state);
    void _updatePWMOutput(float target_pwm);
};

#endif // STATE_MACHINE_H