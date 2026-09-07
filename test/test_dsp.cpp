/**
 * @file test_dsp.cpp
 * @brief Host-native unit test suite for DSP pipeline components (EMA, Biquad, AGC, Gate Interpolation)
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

#include "../include/PinDefinitions.h"
#include "../include/DSP.h"

uint32_t g_mock_millis = 0;
uint32_t millis() { return g_mock_millis; }

void test_alphabeta_filter() {
    AlphaBetaFilter ab(0.5f, 0.1f);

    // First update initializes filter to input value
    float val = ab.update(100.0f, 0.1f);
    assert(std::abs(val - 100.0f) < 1e-5);

    // Step update towards 200.0f
    val = ab.update(200.0f, 0.1f); 
    assert(val > 100.0f && val < 200.0f);

    std::cout << "[PASS] test_alphabeta_filter\n";
}

void test_agc_normalizer() {
    AGCNormalizer agc(10); // 10 sample window

    // Feed values and check bounding [-1.0, 1.0]
    for (int i = 1; i <= 10; ++i) {
        float out = agc.update(static_cast<float>(i));
        assert(out >= -1.0f && out <= 1.0f);
    }
    // Max in window is 10.0, output for 10.0 should be 1.0
    assert(std::abs(agc.getCurrentMax() - 10.0f) < 1e-4);

    std::cout << "[PASS] test_agc_normalizer\n";
}

void test_gate_interpolator() {
    GateInterpolator interpolator(75.0f);
    uint8_t energies[9] = {10, 20, 90, 30, 0, 0, 0, 0, 0};

    int peak_gate = -1;
    float alpha = 0.0f;
    float virtual_e = interpolator.interpolate(180.0f, energies, &peak_gate, &alpha);

    // Peak energy is gate 2 (val = 90)
    assert(peak_gate == 2);
    assert(virtual_e > 0.0f);

    std::cout << "[PASS] test_gate_interpolator\n";
}

void test_biquad_filter() {
    BiquadFilter filter;
    filter.initBandpass(10.0f, 0.1f, 0.5f);

    // Feed a DC signal (0 Hz); DC gain for bandpass should approach 0
    float dc_out = 0.0f;
    for (int i = 0; i < 50; ++i) {
        dc_out = filter.process(10.0f);
    }
    assert(std::abs(dc_out) < 1.0f); // DC attenuated

    std::cout << "[PASS] test_biquad_filter\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "RUNNING NATIVE DSP UNIT TESTS\n";
    std::cout << "========================================\n";
    test_alphabeta_filter();
    test_agc_normalizer();
    test_gate_interpolator();
    test_biquad_filter();
    std::cout << "ALL NATIVE DSP TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
