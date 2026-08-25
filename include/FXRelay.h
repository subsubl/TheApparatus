/**
 * @file FXRelay.h
 * @brief FX Relay Subsystem - Stubbed for Future Circuit-Bend Effects
 * 
 * This module provides a clean interface for 8 relay outputs that will be used
 * for future circuit-bending effects on the video mixer / analog processing chain.
 * Currently implements basic on/off control with configuration storage.
 */

#ifndef FX_RELAY_H
#define FX_RELAY_H

#include "Config.h"

/* ============================================================================
 * FX RELAY CONTROLLER CLASS
 * ============================================================================ */

class FXRelayController {
public:
    FXRelayController();
    
    void begin();
    void update();  // For future timed/sequenced effects
    
    // Individual relay control
    void setRelay(uint8_t index, bool state);
    bool getRelay(uint8_t index) const;
    void toggleRelay(uint8_t index);
    
    // Bulk control
    void setAll(bool state);
    void setPattern(uint8_t pattern);  // Bitmap of 8 relays
    uint8_t getPattern() const;
    
    // Preset effects (stubs for future implementation)
    void triggerEffect(uint8_t effect_id, uint16_t duration_ms = 0);
    void stopAllEffects();
    
    // Configuration
    void setRelayConfig(uint8_t index, const FxRelayConfig& config);
    const FxRelayConfig& getRelayConfig(uint8_t index) const;
    
    // Status
    void printStatus() const;
    
private:
    bool _relay_states[FX_RELAY_COUNT] = {false};
    uint32_t _effect_timers[FX_RELAY_COUNT] = {0};
    bool _effect_active[FX_RELAY_COUNT] = {false};
    
    void _writeRelay(uint8_t index, bool state);
};

// Effect IDs for future use
enum FxEffectId : uint8_t {
    FX_EFFECT_NONE = 0,
    FX_EFFECT_STROBE = 1,        // Rapid on/off
    FX_EFFECT_RAMP = 2,          // Gradual fade
    FX_EFFECT_SEQUENCE = 3,      // Sequential activation
    FX_EFFECT_RANDOM = 4,        // Random pattern
    FX_EFFECT_AUDIO_REACTIVE = 5, // Future: sync to audio
    FX_EFFECT_VIDEO_SYNC = 6     // Future: sync to video frames
};

#endif // FX_RELAY_H