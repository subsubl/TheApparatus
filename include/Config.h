/**
 * @file Config.h
 * @brief Central configuration, pin mappings, constants, and data structures for The Apparatus
 *
 * Hardware: ESP32 DevKit v1
 * Radar: HLK-LD2410 (24GHz mmWave) via UART2 @ 256000 baud, Engineering Mode
 * Touch: Capacitive plate via GPIO interrupt
 * Actuation: Vactrol PWM (LED+LDR) for video mixer crossfader
 * Trigger: Digital GPIO to Raspberry Pi B (mpv IPC)
 * FX: 8x Relay GPIOs (stubbed)
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

/* ============================================================================
 * PIN MAPPINGS (ESP32 DevKit v1 GPIO assignments)
 * ============================================================================ */

// Radar UART (HLK-LD2410) - UART2
#define RADAR_TX_PIN          17  // ESP32 TX -> LD2410 RX
#define RADAR_RX_PIN          16  // ESP32 RX <- LD2410 TX
#define RADAR_BAUD_RATE       256000  // Protocol default: 256k 8N1

// Touch Input - Capacitive plate with interrupt
#define TOUCH_PIN             4   // GPIO 4 - hardware interrupt capable
#define TOUCH_ACTIVE_LEVEL    HIGH  // Configurable: HIGH or LOW

// Vactrol PWM Output - Drives LED inside vactrol (LED+LDR) for mixer crossfader
#define VACTROL_PWM_PIN       18
#define VACTROL_PWM_CHANNEL   0   // LEDC channel 0
#define VACTROL_PWM_FREQ      5000  // 5 kHz (above vactrol LDR response, no visible flicker)
#define VACTROL_PWM_RESOLUTION 8   // 8-bit (0-255)

// Raspberry Pi Trigger Output - Digital GPIO to Pi B (mpv IPC trigger)
#define PI_TRIGGER_PIN        19
#define PI_TRIGGER_ACTIVE_LEVEL HIGH

// FX Relay Outputs - 8 relays for future circuit-bend effects (stubbed)
#define FX_RELAY_PINS         {23, 22, 21, 5, 27, 14, 12, 13}
#define FX_RELAY_COUNT        8
#define FX_RELAY_ACTIVE_LEVEL HIGH

// Status LED (onboard)
#define STATUS_LED_PIN        2

/* ============================================================================
 * LD2410 PROTOCOL CONSTANTS
 * Verified against: HLK-LD2410 Serial Communication Protocol v1.02/v1.07,
 * ESPHome ld2410 component, shabaz123/LD2410 reference parser.
 * ============================================================================ */

// Command frames (host -> radar)
static const uint8_t CMD_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t CMD_FOOTER[4] = {0x04, 0x03, 0x02, 0x01};

// Report frames (radar -> host)
static const uint8_t DATA_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t DATA_FOOTER[4] = {0xF8, 0xF7, 0xF6, 0xF5};

// Command words (little-endian on wire: low byte first)
#define RADAR_CMD_ENABLE_CONFIG    0x00FF  // value 0x0001 required
#define RADAR_CMD_DISABLE_CONFIG   0x00FE
#define RADAR_CMD_READ_PARAMS      0x0061  // NOTE: this is READ PARAMS, not engineering!
#define RADAR_CMD_ENG_MODE_ON      0x0062  // Enable engineering mode
#define RADAR_CMD_ENG_MODE_OFF     0x0063  // Disable engineering mode

// ACK framing
#define RADAR_ACK_WORD             0x0100  // '01 00' after command word in ACK frames
#define RADAR_ACK_STATUS_SUCCESS   0x00

// Report data types (first byte of report frame data)
#define RADAR_REPORT_BASIC         0x02
#define RADAR_REPORT_ENGINEERING   0x01
#define REPORT_HEAD_MARKER         0xAA
#define REPORT_TAIL_MARKER         0x55

// Engineering-mode report payload (after Type byte):
//   AA | target_state(1) | moving_dist(2) | moving_energy(1)
//      | still_dist(2)  | still_energy(1)  | detect_dist(2)
//      | max_moving_gate(1) | max_still_gate(1)
//      | moving_gate_energy[9](1 ea) | still_gate_energy[9](1 ea)
//      | 55 00
// Frame data length = 1(type) + 35 = 36 bytes -> wire len field = 0x0023
#define ENG_PAYLOAD_LEN            35   // bytes after type byte, incl. AA..55 00 markers
#define ENG_FRAME_DATA_LEN         36   // incl. type byte
#define RADAR_GATE_COUNT           9
#define RADAR_GATE_SIZE_CM         75   // default resolution: 75 cm/gate
#define RADAR_MAX_DISTANCE_CM      (RADAR_GATE_COUNT * RADAR_GATE_SIZE_CM)

// Target states
#define TARGET_STATE_NONE          0x00
#define TARGET_STATE_MOVING        0x01
#define TARGET_STATE_STATIONARY    0x02
#define TARGET_STATE_BOTH          0x03

/* ============================================================================
 * STATE MACHINE DEFINITIONS
 * ============================================================================ */

enum ApparatusState_t : uint8_t {
    STATE_IDLE      = 0,  // No target, fader at 0%
    STATE_MACRO     = 1,  // Target moving in range, distance -> fader
    STATE_MICRO     = 2,  // Target stationary, breathing modulation
    STATE_CONTACT   = 3   // Touch override, hard 100% + Pi trigger
};

static const char* const STATE_NAMES[] = {"IDLE", "MACRO", "MICRO", "CONTACT"};

/* ============================================================================
 * CALIBRATION PARAMETERS (stored in NVS via Preferences)
 * ============================================================================ */

struct CalibrationConfig {
    // Distance thresholds (cm)
    float D_min = 50.0f;        // Closest distance = max fader
    float D_max = 300.0f;       // Furthest distance = min fader
    float hysteresis = 20.0f;   // Bidirectional deadband guard at boundaries

    // Macro response shaping
    float gamma_exponent = 1.5f;    // Vactrol LDR linearization exponent

    // Micro (breathing) modulation
    float breathing_depth_M = 0.15f; // Modulation depth fraction (of full scale)

    // Slew rate limiting (inertia glide)
    float slew_rate_limit = 2.0f;   // Max PWM counts change per millisecond

    // PWM output clamping
    uint8_t pwm_min_clamp = 0;
    uint8_t pwm_max_clamp = 255;

    // Touch configuration
    bool touch_inverted = false;

    // Radar tuning
    float variance_threshold_cm = 5.0f;   // Position variance threshold for stationary lock
    uint32_t stationary_lock_time_ms = 1500;

    // DSP
    float agc_epsilon = 1e-6f;
    size_t agc_window_size = 40;          // 4 s @ 10 Hz

    // Network (AP mode default)
    char wifi_ssid[32] = "TheApparatus_AP";
    char wifi_password[64] = "apparatus2024";

    uint16_t telemetry_rate_hz = 20;

    // Version for NVS migration
    uint8_t config_version = 1;
};

#define NVS_NAMESPACE "apparatus"
#define NVS_KEY_CONFIG "calib"

/* ============================================================================
 * RADAR DATA STRUCTURES
 * ============================================================================ */

// Parsed engineering-mode report
struct RadarFrame {
    // Basic information block
    uint8_t  target_state = TARGET_STATE_NONE;
    uint16_t moving_distance_cm = 0;
    uint8_t  moving_energy = 0;
    uint16_t stationary_distance_cm = 0;
    uint8_t  stationary_energy = 0;
    uint16_t detection_distance_cm = 0;   // Authoritative distance used by pipeline

    // Engineering block
    uint8_t  max_moving_gate = 8;
    uint8_t  max_stationary_gate = 8;
    uint8_t  moving_gate_energy[RADAR_GATE_COUNT] = {0};    // Single bytes!
    uint8_t  stationary_gate_energy[RADAR_GATE_COUNT] = {0};

    uint32_t timestamp_ms = 0;
    bool valid = false;
};

/* ============================================================================
 * TELEMETRY DATA STRUCTURE (WebSocket JSON payload)
 * ============================================================================ */

struct TelemetryPacket {
    uint8_t state = 0;
    uint16_t distance_raw = 0;
    float distance_filtered = 0.0f;
    uint16_t stationary_energy[RADAR_GATE_COUNT] = {0};
    float biquad_raw = 0.0f;
    float agc_normalized = 0.0f;
    uint8_t pwm_output = 0;
    bool pi_trigger = false;
    uint32_t timestamp_ms = 0;
    float gamma_shaped = 0.0f;
    float base_pwm = 0.0f;
    int peak_gate = -1;
};

/* ============================================================================
 * FX RELAY CONFIGURATION (Stubbed for future expansion)
 * ============================================================================ */

struct FxRelayConfig {
    uint8_t pin;
    bool active_high;
    bool default_state;
    const char* name;
    const char* description;
};

static const FxRelayConfig FX_RELAY_CONFIGS[FX_RELAY_COUNT] = {
    {23, true, false, "FX_1", "Reserved for future circuit-bend effect"},
    {22, true, false, "FX_2", "Reserved for future circuit-bend effect"},
    {21, true, false, "FX_3", "Reserved for future circuit-bend effect"},
    {5,  true, false, "FX_4", "Reserved for future circuit-bend effect"},
    {27, true, false, "FX_5", "Reserved for future circuit-bend effect"},
    {14, true, false, "FX_6", "Reserved for future circuit-bend effect"},
    {12, true, false, "FX_7", "Reserved for future circuit-bend effect"},
    {13, true, false, "FX_8", "Reserved for future circuit-bend effect"}
};

/* ============================================================================
 * GLOBAL EXTERNS
 * ============================================================================ */

extern CalibrationConfig g_config;
extern Preferences g_preferences;

#endif // CONFIG_H