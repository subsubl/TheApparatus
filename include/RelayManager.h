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
    // new_state valid only then; agc = breathing wave [-1,+1].
    void update(bool state_changed, ApparatusState_t new_state, float agc);

    // === GPIO remapping (GUI-configurable relay pin matrix) ===
    bool remapPin(uint8_t index, uint8_t new_pin);   // Returns false on conflict
    uint8_t getPin(uint8_t index) const { return RELAY_DEFS[index].pin; }

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
    bool getSeqActive(uint8_t index) const { return _rt[index].seq_active; }
    bool isPressed(uint8_t index) const { return _rt[index].level; }
    uint8_t getPattern() const;

    void printStatus() const;

private:
    FxRelayRuntime _rt[RELAY_COUNT] = {};
    uint32_t _last_breath_fire[RELAY_COUNT] = {0};
    uint8_t  _active_pins[RELAY_COUNT];     // Runtime pin assignment

    void _writeLevel(uint8_t index, bool pressed);
    void _sequencerTick(uint8_t i);
    void _clockTick(uint8_t i);
    void _autoTriggerCheck(bool state_changed, ApparatusState_t prev,
                           ApparatusState_t new_state, float agc);
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