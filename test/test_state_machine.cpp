/**
 * @file test_state_machine.cpp
 * @brief Host-native unit test suite for ApparatusStateMachine (IDLE, MACRO, MICRO, CONTACT)
 */

#include <iostream>
#include <cassert>
#include <cmath>

#include "../include/PinDefinitions.h"
#include "../include/DSP.h"

CalibrationConfig g_config;

// Stub millis for native test environment

uint32_t g_mock_millis = 0;
uint32_t millis() { return g_mock_millis; }

#include "../src/DSP.cpp"
#include "../src/StateMachine.cpp"

void test_state_transitions() {
    CalibrationConfig cfg;
    DSPPipeline dsp(cfg);
    ApparatusStateMachine state_machine(dsp);

    // Initial state must be STATE_IDLE
    assert(state_machine.getState() == STATE_IDLE);

    RadarFrame frame;
    frame.timestamp_ms = 100;
    frame.valid = true;
    frame.target_state = TARGET_STATE_MOVING;
    frame.detection_distance_cm = 200; // Inside [D_min=60, D_max=450] range


    // Update with touch=false -> should transition from IDLE to MACRO
    state_machine.update(false, frame, true, 0.0f, 0.0f);
    assert(state_machine.getState() == STATE_MACRO);

    // Touch override -> first tick starts debounce, second tick after >50ms resolves touch
    state_machine.update(true, frame, true, 0.0f, 0.0f);
    g_mock_millis += 100;
    state_machine.update(true, frame, true, 0.0f, 0.0f);
    assert(state_machine.getState() == STATE_CONTACT);

    // Release touch -> first tick starts debounce, second tick after >50ms resolves release
    state_machine.update(false, frame, true, 0.0f, 0.0f);
    g_mock_millis += 100;
    state_machine.update(false, frame, true, 0.0f, 0.0f);
    assert(state_machine.getState() == STATE_MACRO);



    std::cout << "[PASS] test_state_transitions\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "RUNNING NATIVE STATE MACHINE TESTS\n";
    std::cout << "========================================\n";
    test_state_transitions();
    std::cout << "ALL STATE MACHINE TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
