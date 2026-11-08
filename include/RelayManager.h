/**
 * @file RelayManager.h
 * @brief WJ-AVE5 button spoofing via 8-ch active-LOW relay board
 *
 * Actuation sources:
 *   1. Physical performer buttons (tap / multi-click / long-press-hold)
 *   2. GUI WebSocket commands
 *   3. Automatic triggers per-relay: state entries + breath peaks
 *
 * Press shaping is fully non-blocking (deadline-based sequencer).
 * Electrical polarity: LOW = pressed, HIGH = released.
 */

#ifndef RELAY_MANAGER_H
#define RELAY_MANAGER_H

#include "PinDefinitions.h"

class RelayManager {
public:
    void begin();

    // Poll every loop. state_changed: SM transitioned this tick;
    // Must be called at loop rate to run non-blocking delays
    // and process layer-linked triggers.
    void update(bool state_changed, ApparatusState_t new_state, float agc, float velocity_cm_s);

    // === GPIO remapping and dynamic channels ===
    bool remapPin(uint8_t index, int new_pin);
    bool addRelay(int pin = -1);
    bool removeLastRelay();
    int getPin(uint8_t index) const {
        return (index < g_config.relay_count) ? g_config.layout.relay_pins[index] : -1;
    }
    uint8_t getChannelCount() const { return g_config.relay_count; }

    // Sequenced firing (non-blocking)
    void fireSequence(uint8_t index);                       // Configured shape
    void fireSequenceCustom(uint8_t index, uint16_t length_ms,
                            uint8_t count, uint16_t gap_ms); // Explicit shape
    void stopSequence(uint8_t index);

    // Momentary hold (performer long-press)
    void startHold(uint8_t index);
    void endHold(uint8_t index);

    // Direct level control (cancels sequencing on that channel)
    void setPressed(uint8_t index, bool pressed);
    void setAllReleased();

    // Status
    bool getSeqActive(uint8_t index) const { return (index < BANK_CAPACITY) ? _rt[index].seq_active : false; }
    bool isPressed(uint8_t index) const { return (index < BANK_CAPACITY) ? _rt[index].level : false; }
    uint16_t getPattern() const;

    void printStatus() const;

private:
    FxRelayRuntime _rt[BANK_CAPACITY] = {};
    uint32_t _last_breath_fire[BANK_CAPACITY] = {0};
    uint32_t _last_zero_fire = 0;
    float    _last_velocity = 0.0f;
    bool     _lunge_latched = false;
    bool     _retreat_latched = false;

    void _writeLevel(uint8_t index, bool pressed);
    void _sequencerTick(uint8_t i);
    void _clockTick(uint8_t i);
    void _autoTriggerCheck(bool state_changed, ApparatusState_t prev,
                           ApparatusState_t new_state, float agc, float velocity_cm_s);
    bool _cooldownOk(uint8_t index) const;
};

// ---------------------------------------------------------------------------
// Performer buttons: tap / multi-click / long-press detection
// ---------------------------------------------------------------------------
class ButtonBank {
public:
    void begin();
    void update(RelayManager& relays);

private:
    struct BtnState {
        bool     last_raw = false;
        bool     stable = false;
        uint32_t last_change_ms = 0;
        uint8_t  click_count = 0;
        uint32_t first_click_ms = 0;
        bool     long_fired = false;
        uint32_t down_since_ms = 0;
        bool     is_down = false;
    };
    BtnState _st[BUTTON_MAX];

    void _handleTap(uint8_t i, uint8_t clicks, RelayManager& fx);
};

#endif // RELAY_MANAGER_H