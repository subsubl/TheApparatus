/**
 * @file DSP.cpp
 * @brief DSP Pipeline Implementation
 */

#include "DSP.h"
#include <Arduino.h>

/* ============================================================================
 * CONSTRUCTOR
 * ============================================================================ */

DSPPipeline::DSPPipeline(const CalibrationConfig& config) : _config(config) {
    // EMA: 500 ms time constant at 10 Hz sampling
    // alpha = 1 - exp(-dt/tau) = 1 - exp(-0.1/0.5) ≈ 0.1813
    _distance_ema.setAlpha(1.0f - expf(-0.1f / 0.5f));

    // Peak-gate + centroid interpolation
    _gate_interpolator = GateInterpolator(RADAR_GATE_SIZE_CM);

    // Respiration bandpass
    _biquad.initBandpass(10.0f, 0.1f, 0.5f);

    // AGC: 40 samples = 4 seconds @ 10 Hz
    _agc = AGCNormalizer(_config.agc_window_size, _config.agc_epsilon);
}

/* ============================================================================
 * MAIN PROCESS FUNCTION (called per radar frame, internally rate-limited to 10 Hz)
 * ============================================================================ */

bool DSPPipeline::process(const RadarFrame& frame,
                          float& out_normalized_resp,
                          float& out_biquad_raw) {
    uint32_t now = millis();

    if (_sample_count > 0 && (now - _last_sample_time < SAMPLE_PERIOD_MS)) {
        return false;  // Not time for a new pipeline sample yet
    }
    _last_sample_time = now;
    _sample_count++;

    // === STAGE 1: EMA on raw distance ===
    float raw_distance = (float)frame.detection_distance_cm;   // Authoritative field
    if (frame.target_state == TARGET_STATE_NONE || raw_distance <= 0.0f) {
        raw_distance = RADAR_MAX_DISTANCE_CM;  // Treat as "no target" for filter continuity
    }
    float filtered_distance = _distance_ema.update(raw_distance);

    // === STAGE 2: Peak-gate selection + sub-gate centroid interpolation ===
    int peak_gate = -1;
    float alpha = 0.0f;
    float virtual_energy = 0.0f;

    if (frame.valid && frame.target_state != TARGET_STATE_NONE) {
        virtual_energy = _gate_interpolator.interpolate(
            filtered_distance,
            frame.stationary_gate_energy,
            &peak_gate,
            &alpha);
    }

    _current_peak_gate = peak_gate;
    _last_virtual_energy = virtual_energy;
    _last_alpha = alpha;

    // === STAGE 3: Biquad bandpass ===
    float biquad_output = _biquad.process(virtual_energy);
    _last_biquad_output = biquad_output;

    // === STAGE 4: Sliding-window AGC ===
    float normalized = _agc.update(biquad_output);
    _last_agc_output = normalized;

    out_normalized_resp = normalized;
    out_biquad_raw = biquad_output;
    return true;
}

/* ============================================================================
 * RESET
 * ============================================================================ */

void DSPPipeline::reset() {
    _distance_ema.reset();
    _biquad.reset();
    _agc.clear();
    _sample_count = 0;
    _last_sample_time = 0;
    _current_peak_gate = -1;
    _last_virtual_energy = 0.0f;
    _last_alpha = 0.0f;
    _last_biquad_output = 0.0f;
    _last_agc_output = 0.0f;
}