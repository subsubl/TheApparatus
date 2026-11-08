/**
 * @file PinDefinitions.h
 * @brief Central pin mappings, protocol constants, and persisted configuration
 *
 * Hardware: ESP32-WROOM-32 (3.3V logic)
 * Mixer: Modified Panasonic WJ-AVE5
 *   - 6x slider control lines driven by vactrols (LED-LDR, optically isolated
 *     across the slider wiper nodes) via LEDC PWM
 *   - 8x effect buttons spoofed by an active-LOW relay board (closing a relay
 *     presses the button electrically)
 * Radar: HLK-LD2410 Engineering Mode via UART2
 * Pi link: Serial1 -> Raspberry Pi mpv daemon (LOOP_A / LOOP_B / TRIGGER_SEEK)
 */

#ifndef PIN_DEFINITIONS_H
#define PIN_DEFINITIONS_H

#ifdef APPARATUS_NATIVE_TEST
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cmath>
#ifndef PI
#define PI 3.14159265358979323846f
#endif
#ifndef HIGH
#define HIGH 1
#define LOW  0
#endif
#ifndef constrain
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#endif
#ifndef log_i
#define log_i(...)
#define log_e(...)
#define log_w(...)
#define log_d(...)
#endif
uint32_t millis();
class Preferences {
public:
    bool begin(const char*, bool) { return true; }
    bool isKey(const char*) { return false; }
    size_t getBytesLength(const char*) { return 0; }
    size_t getBytes(const char*, void*, size_t) { return 0; }
    size_t putBytes(const char*, const void*, size_t) { return 0; }
    void clear() {}
};
#else
#include <Arduino.h>
#include <Preferences.h>
#endif

#include "Configuration.h"
using namespace apparatus;
using FxRelayTrigger = apparatus::FxTrigger;
/* ============================================================================
 * VACTROL PWM OUTPUTS - 6 channels, LEDC peripheral
 * Wiring: GPIO -> 150R -> vactrol LED anode -> GND (~10 mA max per pin)
 * LDR side bridges the WJ-AVE5 slider wiper nodes.
 * ============================================================================ */

#define VACTROL_COUNT           6
#define VACTROL_PWM_FREQ        5000      // 5 kHz
#define VACTROL_PWM_RESOLUTION  10        // 10-bit: 0..1023
#define VACTROL_PWM_MAX         ((1 << VACTROL_PWM_RESOLUTION) - 1)

enum VactrolChannel : uint8_t {
    VACT_MIX = 0,       // GPIO 13 - T-Bar / crossfade wiper
    VACT_COLOR_X = 1,   // GPIO 14 - Color X
    VACT_COLOR_Y = 2,   // GPIO 25 - Color Y
    VACT_WIPE_SPEED = 3,// GPIO 26 - Wipe speed
    VACT_EFFECT_LVL = 4,// GPIO 27 - Effect level
    VACT_AUX = 5        // GPIO 33 - Aux modulation
};

struct VactrolPinDef {
    uint8_t pin;
    uint8_t ledc_ch;
    const char* label;
    const char* mixer_node;
};

static const VactrolPinDef VACTROL_DEFS[VACTROL_COUNT] = {
    {13, 0, "Mix/T-Bar",   "E2 pin 5"},
    {14, 1, "Color X",     "Color X wiper"},
    {25, 2, "Color Y",     "Color Y wiper"},
    {26, 3, "Wipe Speed",  "Wipe speed wiper"},
    {27, 4, "Effect Level","Effect level wiper"},
    {33, 5, "Aux Mod",     "Aux node"}
};

/* ============================================================================
 * RELAY BOARD - 8-channel 5V module, ACTIVE-LOW sinking optocoupler logic
 * JD-VCC jumper removed: external 5V on JD-VCC, ESP32 3.3V on header VCC,
 * grounds isolated. Output LOW = relay ON (button pressed), HIGH = released.
 * GPIO15 is a boot strap pin: keep HIGH at reset to avoid SPI-flash boot mode.
 * ============================================================================ */

#define RELAY_COUNT             8

struct RelayPinDef {
    uint8_t pin;
    const char* label;
};

static const RelayPinDef RELAY_DEFS[RELAY_COUNT] = {
    {4,  "WJ-BTN1"}, {18, "WJ-BTN2"}, {19, "WJ-BTN3"}, {21, "WJ-BTN4"},
    {22, "WJ-BTN5"}, {23, "WJ-BTN6"}, {32, "WJ-BTN7"}, {15, "WJ-BTN8"}
};
#define RELAY_BOOT_SAFE_LEVEL   HIGH   // All relays OFF at boot

/* ============================================================================
 * MMWAVE RADAR - HLK-LD2410 on UART2
 * ============================================================================ */

#define RADAR_TX_PIN            17
#define RADAR_RX_PIN            16
#define RADAR_BAUD_RATE         256000

// Protocol constants (verified vs. datasheet v1.02/v1.07 + ESPHome)
static const uint8_t CMD_HEADER[4]  = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t CMD_FOOTER[4]  = {0x04, 0x03, 0x02, 0x01};
static const uint8_t DATA_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t DATA_FOOTER[4] = {0xF8, 0xF7, 0xF6, 0xF5};

#define RADAR_CMD_ENABLE_CONFIG    0x00FF
#define RADAR_CMD_DISABLE_CONFIG   0x00FE
#define RADAR_CMD_READ_PARAMS      0x0061
#define RADAR_CMD_ENG_MODE_ON      0x0062
#define RADAR_CMD_ENG_MODE_OFF     0x0063
#define RADAR_ACK_WORD             0x0100
#define RADAR_ACK_STATUS_SUCCESS   0x00
#define RADAR_REPORT_BASIC         0x02
#define RADAR_REPORT_ENGINEERING   0x01
#define REPORT_HEAD_MARKER         0xAA
#define REPORT_TAIL_MARKER         0x55
#define ENG_PAYLOAD_LEN            35

#define RADAR_GATE_COUNT           9
#define RADAR_GATE_SIZE_CM         75
#define RADAR_MAX_DISTANCE_CM      (RADAR_GATE_COUNT * RADAR_GATE_SIZE_CM)

#define TARGET_STATE_NONE          0x00
#define TARGET_STATE_MOVING        0x01
#define TARGET_STATE_STATIONARY    0x02
#define TARGET_STATE_BOTH          0x03

/* ============================================================================
 * PERFORMER BUTTONS + TOUCH OVERRIDE
 * Touch uses the ESP32 INTERNAL touch peripheral on T4 = GPIO 13.
 * Wire the metal plate directly to GPIO13 through a small series resistor
 * (220R-1k); a ~1 nF cap from the pin to GND stabilizes readings. No external
 * pull resistors needed - the peripheral handles everything.
 * ============================================================================ */

// Capacitive touch plate (CONTACT override). ESP32 INTERNAL touch peripheral,
// pad T5 = GPIO 12 - the only touch-capable pin free in our matrix, so NO
// other signals move. Wire: plate -> 1k series -> GPIO12, plus 10k pull-down
// to GND at the pin. The pull-down doubles as boot insurance: GPIO12 is a
// strapping pin (flash-voltage select) and must read LOW through reset.
#define TOUCH_PIN               12   // T5 touch-capable (free in our matrix)
#define TOUCH_PAD_INDEX         5    // T5
#define TOUCH_ACTIVE_LEVEL      HIGH // legacy digitalRead semantic (unused w/ touchRead)

// 8 performer buttons. ACTIVE-HIGH wiring: pin --[button]-- 3V3.
// GPIOs 25/26 are shared with vactrol PWM in this matrix and therefore NOT
// usable as buttons; the button bank uses free inputs instead:
//   35, 36, 39 (input-only, external pull-downs REQUIRED)
//   34 freed (was touch; now spare input)
// To keep exactly 8 buttons we accept 7 physical buttons mapped to relays
// 1..7 (relay 8 remains GUI/auto-only), OR wire additional via I2C expander.
#define BUTTON_MAX              7
static const uint8_t BUTTON_PINS[BUTTON_MAX] = {35, 36, 39};  // 3 wired by default
#define BUTTON_ACTIVE_LEVEL     HIGH

/* ============================================================================
 * RASPBERRY PI LINK - Serial1 (UART1) over USB/UART0-style wiring
 * ESP32 TX2? No: UART1 remapped away from flash pins.
 * TX = GPIO 1 is the USB serial... use safe remap:
 *   PI_LINK_TX = GPIO 33? (taken by vactrol 6) -> use GPIO 2 (free, also LED)
 *   PI_LINK_RX = GPIO 14? (taken) -> leave RX unconnected (TX-only protocol)
 * Final: PI_LINK uses Serial1 on TX=GPIO2, baud 115200, TX-only commands.
 * ============================================================================ */

#define PI_LINK_TX_PIN          2
#define PI_LINK_BAUD            115200

// Status LED shares GPIO 2 with Pi-link TX on some boards; LED use is
// non-critical heartbeat so sharing is acceptable.
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN          2
#endif

/* ============================================================================
 * STATE MACHINE
 * ============================================================================ */

enum ApparatusState_t : uint8_t {
    STATE_IDLE      = 0,
    STATE_MACRO     = 1,
    STATE_MICRO     = 2,
    STATE_CONTACT   = 3
};

static const char* const STATE_NAMES[] = {"IDLE", "MACRO", "MICRO", "CONTACT"};

/* ============================================================================
 * FX RELAY SYSTEM - trigger conditions & press shaping
 * ============================================================================ */

static const char* const TRIGGER_NAMES[] = {
    "Manual",
    "Layer 3 Cut (Contact)",
    "Layer 3 Release (un-latch)",
    "Layer 2 Entry (Macro)",
    "Layer 1 Return (Idle)",
    "Breath Lock (Micro)",
    "Inhale Peak",
    "Exhale Peak",
    "Lunge",
    "Retreat",
    "Velocity Zero"
};
#define FX_TRIGGER_COUNT 11

struct FxRelayRuntime {
    bool     seq_active;
    bool     level;            // Current electrical level (LOW=pressed for active-LOW board)
    bool     momentary_hold;   // Held closed by long-press
    uint8_t  presses_left;
    uint8_t  total_presses;    // For GUI telemetry of multi-press sequences
    bool     in_press;
    uint32_t stage_deadline;
    uint32_t last_auto_fire;
    uint32_t clock_last_fire;
};

// ============================================================================
// BOOT SEQUENCER - power-on ritual for the WJ-AVE5 (non-blocking)
// ============================================================================
class BootSequencer {
public:
    void start();                     // Kick off after config load + relay init
    void update(class RelayManager& relays);
    bool running() const { return _active; }
private:
    bool     _active = false;
    uint8_t  _current_step = 0;
    uint8_t  _presses_done = 0;       // Within current step
    bool     _await_gap_or_dwell = false;
    uint32_t _deadline = 0;
};

#define BOOT_MAX_STEPS 12
#define CONFIG_VERSION 5

#define NVS_NAMESPACE "apparatus"
#define NVS_KEY_CONFIG "calib"

/* ============================================================================
 * RADAR FRAME
 * ============================================================================ */

struct RadarFrame {
    uint8_t  target_state = TARGET_STATE_NONE;
    uint16_t moving_distance_cm = 0;
    uint8_t  moving_energy = 0;
    uint16_t stationary_distance_cm = 0;
    uint8_t  stationary_energy = 0;
    uint16_t detection_distance_cm = 0;
    uint8_t  max_moving_gate = 8;
    uint8_t  max_stationary_gate = 8;
    uint8_t  moving_gate_energy[RADAR_GATE_COUNT] = {0};
    uint8_t  stationary_gate_energy[RADAR_GATE_COUNT] = {0};
    uint32_t timestamp_ms = 0;
    bool valid = false;
};

/* ============================================================================
 * TELEMETRY PACKET
 * ============================================================================ */

struct TelemetryPacket {
    uint8_t state = 0;
    uint16_t distance_raw = 0;
    float distance_filtered = 0.0f;
    uint16_t stationary_energy[RADAR_GATE_COUNT] = {0};
    float biquad_raw = 0.0f;
    float agc_normalized = 0.0f;
    uint16_t mix_pwm = 0;
    bool pi_trigger = false;
    uint32_t timestamp_ms = 0;
    float gamma_shaped = 0.0f;
    float base_pwm_f = 0.0f;
    int peak_gate = -1;
    bool buttons[BUTTON_MAX] = {false};
    bool relay_seq[BANK_CAPACITY] = {false};
    bool relay_pressed[BANK_CAPACITY] = {false};
    uint16_t vactrol_val[BANK_CAPACITY] = {0};
    bool vactrol_auto[BANK_CAPACITY] = {false};
};

extern CalibrationConfig g_config;
extern Preferences g_preferences;

#endif // PIN_DEFINITIONS_H