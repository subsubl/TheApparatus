/**
 * @file DSP.h
 * @brief Digital Signal Processing Pipeline for Respiration Extraction
 *
 * Reconciles both specs:
 *  - Master briefing: "isolate the peak gate (G_target)" from the 9-gate array
 *  - Original spec:   sub-gate centroid interpolation to kill step artifacts
 * Implementation: select peak stationary-energy gate, then interpolate between
 * it and its neighbor using fractional distance within the gate.
 *
 * Stages:
 *  1. 500 ms EMA on raw distance
 *  2. Peak-gate selection + sub-gate centroid interpolation
 *  3. Biquad bandpass 0.1-0.5 Hz @ 10 Hz (bilinear-transform design)
 *  4. 4-second sliding-window AGC -> normalized [-1, +1]
 */

#ifndef DSP_H
#define DSP_H

#include "Config.h"
#include <cmath>
#include <vector>

/* ============================================================================
 * EMA FILTER
 * ============================================================================ */

class EMAFilter {
public:
    explicit EMAFilter(float alpha = 0.181f) : _alpha(alpha), _initialized(false), _value(0.0f) {}

    void setAlpha(float alpha) { _alpha = constrain(alpha, 0.001f, 1.0f); }
    float getAlpha() const { return _alpha; }

    float update(float input) {
        if (!_initialized) {
            _value = input;
            _initialized = true;
        } else {
            _value = _alpha * input + (1.0f - _alpha) * _value;
        }
        return _value;
    }

    float getValue() const { return _value; }
    bool isInitialized() const { return _initialized; }
    void reset(float initial = 0.0f) { _value = initial; _initialized = false; }

private:
    float _alpha;
    bool _initialized;
    float _value;
};

/* ============================================================================
 * PEAK-GATE SELECTION + SUB-GATE CENTROID INTERPOLATION
 * ============================================================================ */

class GateInterpolator {
public:
    explicit GateInterpolator(float gate_size_cm = RADAR_GATE_SIZE_CM)
        : _gate_size(gate_size_cm), _last_peak_gate(-1) {}

    // Selects peak stationary-energy gate G_target, then linearly interpolates
    // its energy with the neighbor gate based on fractional position within the
    // target's gate: alpha = fmod(filtered_distance, 75) / 75
    //
    // Interpolation partner choice:
    //   - if the neighbor toward the viewer has >= energy, blend that way
    //     (target likely drifting across the boundary)
    //   - else blend with the next-higher gate
    // This preserves the "virtual energy" continuity of the original spec while
    // always anchoring on the dominant (peak) gate required by the briefing.
    float interpolate(float filtered_distance_cm,
                      const uint8_t* stationary_gate_energy,
                      int* out_peak_gate = nullptr,
                      float* out_alpha = nullptr) {
        _last_peak_gate = -1;
        if (out_alpha) *out_alpha = 0.0f;

        if (!stationary_gate_energy || filtered_distance_cm <= 0.0f ||
            filtered_distance_cm >= RADAR_MAX_DISTANCE_CM) {
            if (out_peak_gate) *out_peak_gate = -1;
            return 0.0f;
        }

        // Fractional position within gate (original spec formula)
        int gate_of_target = (int)(filtered_distance_cm / _gate_size);
        gate_of_target = constrain(gate_of_target, 0, RADAR_GATE_COUNT - 1);
        float alpha = fmodf(filtered_distance_cm, _gate_size) / _gate_size;
        alpha = constrain(alpha, 0.0f, 1.0f);

        // Peak-gate selection (master briefing): highest stationary energy wins.
        int peak_gate = 0;
        uint8_t peak_val = stationary_gate_energy[0];
        for (int i = 1; i < RADAR_GATE_COUNT; i++) {
            if (stationary_gate_energy[i] > peak_val) {
                peak_val = stationary_gate_energy[i];
                peak_gate = i;
            }
        }
        _last_peak_gate = peak_gate;

        // Anchor on peak gate; interpolate toward the adjacent gate indicated by
        // the target's fractional position (sway direction).
        float e_peak = (float)stationary_gate_energy[peak_gate];
        int partner = (gate_of_target > peak_gate && peak_gate < RADAR_GATE_COUNT - 1)
                        ? peak_gate + 1 : ((peak_gate > 0) ? peak_gate - 1 : peak_gate + 1);
        partner = constrain(partner, 0, RADAR_GATE_COUNT - 1);
        float e_partner = (float)stationary_gate_energy[partner];

        // Blend weight: how far the target sits into the peak gate along the
        // sway axis. At boundaries this yields smooth cross-fade instead of a step.
        float w = (partner > peak_gate) ? alpha : (1.0f - alpha);
        w = constrain(w, 0.0f, 1.0f);

        float evirtual = (1.0f - w) * e_peak + w * e_partner;

        if (out_peak_gate) *out_peak_gate = peak_gate;
        if (out_alpha) *out_alpha = alpha;
        return evirtual;
    }

    int getLastPeakGate() const { return _last_peak_gate; }

private:
    float _gate_size;
    int _last_peak_gate;
};

/* ============================================================================
 * BIQUAD BANDPASS FILTER (RBJ audio-EQ-cookbook bilinear design)
 * ============================================================================ */

class BiquadFilter {
public:
    BiquadFilter() : _coeffs{0.0f}, _state{0.0f} {}

    void initBandpass(float sample_rate_hz, float low_hz, float high_hz) {
        _sample_rate = sample_rate_hz;

        float f0 = sqrtf(low_hz * high_hz);          // Geometric center (~0.2236 Hz)
        float Q = f0 / (high_hz - low_hz);           // Q ≈ 0.559 for 0.1-0.5 Hz

        float w0 = 2.0f * PI * f0 / _sample_rate;
        float alpha = sinf(w0) / (2.0f * Q);

        float cosw0 = cosf(w0);

        // RBJ constant-skirt-gain bandpass (peak gain = Q):
        float b0 = alpha;
        float b1 = 0.0f;
        float b2 = -alpha;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cosw0;
        float a2 = 1.0f - alpha;

        // Normalize so a0 = 1
        _coeffs.b0 = b0 / a0;
        _coeffs.b1 = b1 / a0;
        _coeffs.b2 = b2 / a0;
        _coeffs.a1 = a1 / a0;
        _coeffs.a2 = a2 / a0;

        reset();

        log_i("Biquad BP: fs=%.1f f0=%.3fHz Q=%.3f | b0=%.6f b1=%.6f b2=%.6f a1=%.6f a2=%.6f",
              _sample_rate, f0, Q, _coeffs.b0, _coeffs.b1, _coeffs.b2, _coeffs.a1, _coeffs.a2);
    }

    float process(float x) {
        float y = _coeffs.b0 * x + _coeffs.b1 * _state.x1 + _coeffs.b2 * _state.x2
                - _coeffs.a1 * _state.y1 - _coeffs.a2 * _state.y2;
        _state.x2 = _state.x1;
        _state.x1 = x;
        _state.y2 = _state.y1;
        _state.y1 = y;
        return y;
    }

    void reset() {
        _state.x1 = _state.x2 = 0.0f;
        _state.y1 = _state.y2 = 0.0f;
    }

private:
    struct Coeffs { float b0, b1, b2, a1, a2; };
    struct State  { float x1, x2, y1, y2; };

    float _sample_rate = 10.0f;
    Coeffs _coeffs;
    State _state;
};

/* ============================================================================
 * SLIDING-WINDOW AGC
 * ============================================================================ */

class AGCNormalizer {
public:
    AGCNormalizer(size_t window_size = 40, float epsilon = 1e-6f)
        : _window_size(window_size), _epsilon(epsilon),
          _buffer(std::vector<float>(window_size, 0.0f)) {}

    float update(float input) {
        _buffer[_head] = input;
        _head = (_head + 1) % _window_size;
        if (_count < _window_size) _count++;

        float max_abs = _epsilon;
        for (size_t i = 0; i < _count; i++) {
            float a = fabsf(_buffer[i]);
            if (a > max_abs) max_abs = a;
        }
        _current_max = max_abs;

        float n = input / max_abs;
        return constrain(n, -1.0f, 1.0f);
    }

    float getCurrentMax() const { return _current_max; }
    bool isWindowFull() const { return _count == _window_size; }

    void clear() {
        std::fill(_buffer.begin(), _buffer.end(), 0.0f);
        _head = 0;
        _count = 0;
        _current_max = _epsilon;
    }

    void resize(size_t new_size) {
        _window_size = new_size;
        _buffer.assign(new_size, 0.0f);
        clear();
    }

private:
    size_t _window_size;
    size_t _head = 0;
    size_t _count = 0;
    float _epsilon;
    std::vector<float> _buffer;
    float _current_max;
};

/* ============================================================================
 * COMPLETE DSP PIPELINE
 * ============================================================================ */

class DSPPipeline {
public:
    explicit DSPPipeline(const CalibrationConfig& config);

    // Process one radar frame at the 10 Hz pipeline rate.
    // Returns true when a fresh pipeline sample was produced.
    bool process(const RadarFrame& frame, float& out_normalized_resp, float& out_biquad_raw);

    // Telemetry getters
    float getDistanceFiltered() const { return _distance_ema.getValue(); }
    float getBiquadRaw() const { return _last_biquad_output; }
    float getAGCNormalized() const { return _last_agc_output; }
    int getPeakGate() const { return _current_peak_gate; }
    float getVirtualEnergy() const { return _last_virtual_energy; }
    float getGateAlpha() const { return _last_alpha; }

    // Live reconfiguration
    void updateEMAAlpha(float alpha) { _distance_ema.setAlpha(alpha); }
    void updateAGCWindow(size_t size) { _agc.resize(size); }
    void reset();

private:
    const CalibrationConfig& _config;

    EMAFilter _distance_ema;
    GateInterpolator _gate_interpolator;
    BiquadFilter _biquad;
    AGCNormalizer _agc;

    int _current_peak_gate = -1;
    float _last_virtual_energy = 0.0f;
    float _last_alpha = 0.0f;
    float _last_biquad_output = 0.0f;
    float _last_agc_output = 0.0f;
    uint32_t _sample_count = 0;
    uint32_t _last_sample_time = 0;

    static constexpr uint32_t SAMPLE_PERIOD_MS = 100;  // 10 Hz
};

#endif // DSP_H