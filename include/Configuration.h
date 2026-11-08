#pragma once
// Configuration module (02a) for subsubl/TheApparatus.
//
// Persistence contract: the NVS glue lives in a later module and must store
// under namespace "apparatus", key "calib". The stored blob is the
// explicit-field little-endian envelope emitted by configurationEncode()
// (magic 'APP5'), NEVER a raw struct image, which would bake in ABI,
// padding and endianness. Portable C++17: no Arduino headers, no
// exceptions/RTTI/heap; every error string here is a static literal.
#include <array>
#include <cstddef>
#include <cstdint>

#include "OutputAllocation.h"

namespace apparatus {

// ---- Settings types (frozen v4 member names/semantics) ----

struct VactrolSettings {
    uint8_t  source_mode = 0;   // 0:Manual, 1:Distance, 2:Speed, 3:Breath, 4:Lunge
    bool     dynamic_slew = false;
    uint16_t min_clamp = 0;
    uint16_t max_clamp = 1023;
    float    slew_per_ms = 2.0f;
    uint16_t manual_value = 0;
    uint8_t  ave5_pot = 0;      // index into AVE5_POTS
    float    gamma = 1.0f;
};

struct FxRelaySettings {
    bool     enabled = false;
    uint8_t  trigger = 0;       // FxTrigger value
    uint16_t press_length_ms = 120;
    uint8_t  press_count = 1;
    uint16_t press_gap_ms = 150;
    bool     clock_enable = false;
    uint32_t clock_interval_ms = 5000;
    char     name[12] = "";     // NUL-terminated within 12 bytes
    uint8_t  ave5_button = 0;   // index into AVE5_BUTTONS
};

struct BootStep {
    uint8_t  relay;             // relay index in relay bank
    uint8_t  presses;
    uint16_t length_ms;
    uint16_t gap_ms;
    uint16_t wait_after_ms;
};

struct BootSettings {
    bool     enabled = true;
    uint16_t start_delay_ms = 3000;
    uint8_t  step_count = 2;
    BootStep steps[12] = {
        {0, 1, 400, 150, 2000},
        {1, 1, 120, 150, 500}
    };
};

enum FxTrigger {
    TRIG_MANUAL = 0,
    TRIG_ON_L3_CUT = 1,
    TRIG_ON_L3_RELEASE = 2,
    TRIG_ON_L2_ENTRY = 3,
    TRIG_ON_L1_RETURN = 4,
    TRIG_ON_BREATH_LOCK = 5,
    TRIG_INHALE = 6,
    TRIG_EXHALE = 7,
    TRIG_ON_LUNGE = 8,
    TRIG_ON_RETREAT = 9,
    TRIG_VELOCITY_ZERO = 10
};

// ---- AVE5 catalogs ----
inline constexpr const char* AVE5_BUTTONS[12] = {
    "(unassigned)", "STILL (freeze)", "STROBE", "MOSAIC", "PAINT", "NEGATIVE",
    "CUT (bus switch)", "A/B (bus select)", "WIPE (arm)", "WIPE PATTERN select",
    "SUPERIMPOSE (camera key)", "FADE (auto fade)"};
inline constexpr const char* AVE5_POTS[8] = {
    "(free use / unlabeled)", "Mix/T-Bar lever", "Color balance X",
    "Color balance Y", "Wipe speed", "Effect level", "Fade lever", "Audio level"};
inline constexpr int AVE5_BUTTON_COUNT =
    static_cast<int>(sizeof(AVE5_BUTTONS) / sizeof(AVE5_BUTTONS[0]));
inline constexpr int AVE5_POT_COUNT =
    static_cast<int>(sizeof(AVE5_POTS) / sizeof(AVE5_POTS[0]));

// ---- v5 configuration (defaults form full WJ-AVE5 preset: 6 vactrols + 8 relays) ----
struct CalibrationConfig {
    float D_min = 50.0f;
    float D_max = 300.0f;
    float hysteresis = 20.0f;
    float gamma_exponent = 1.5f;
    float slew_rate_limit = 2.0f;
    float breathing_depth_M = 0.15f;
    uint8_t pwm_min_clamp = 0;
    uint8_t pwm_max_clamp = 255;
    bool touch_inverted = false;
    float variance_threshold_cm = 5.0f;
    uint32_t stationary_lock_time_ms = 1500;
    float agc_epsilon = 1e-6f;
    uint16_t agc_window_size = 40;
    uint32_t button_debounce_ms = 30;
    uint32_t multiclick_window_ms = 320;
    uint32_t long_press_ms = 650;
    float breath_threshold = 0.35f;
    float lunge_threshold_cm_s = -40.0f;
    float retreat_threshold_cm_s = 30.0f;
    uint32_t auto_trigger_cooldown_ms = 2500;
    uint8_t gate_sensitivity[9] = {50, 50, 50, 50, 50, 50, 50, 50, 50};
    float pi_zone_far_cm = 350.0f;
    float pi_zone_near_cm = 120.0f;
    char wifi_ssid[32] = "TheApparatus_AP";
    char wifi_password[64] = "apparatus2024";
    uint16_t telemetry_rate_hz = 20;
    uint8_t config_version = 5;
    int vactrol_count = 6;
    int relay_count = 8;
    OutputLayout layout{
        6, 8,
        {{13, 14, 25, 26, 27, 33, -1, -1, -1, -1, -1, -1, -1, -1}},
        {{4, 18, 19, 21, 22, 23, 32, 15, -1, -1, -1, -1, -1, -1}}
    };
    std::array<VactrolSettings, 14> vactrol{{
        {1, false, 0, 1023, 2.0f, 0, 1, 1.5f},
        {0, false, 0, 1023, 2.0f, 0, 2, 1.0f},
        {0, false, 0, 1023, 2.0f, 0, 3, 1.0f},
        {0, false, 0, 1023, 2.0f, 0, 4, 1.0f},
        {0, false, 0, 1023, 2.0f, 0, 5, 1.0f},
        {0, false, 0, 1023, 2.0f, 0, 6, 1.0f},
        {}, {}, {}, {}, {}, {}, {}, {}
    }};
    std::array<FxRelaySettings, 14> fx{{
        {true, TRIG_MANUAL, 120, 1, 150, false, 5000, "WJ-BTN1", 1},
        {true, TRIG_MANUAL, 120, 1, 150, false, 5000, "WJ-BTN2", 2},
        {true, TRIG_MANUAL, 120, 1, 150, false, 5000, "WJ-BTN3", 3},
        {true, TRIG_MANUAL, 120, 1, 150, false, 5000, "WJ-BTN4", 4},
        {true, TRIG_MANUAL, 120, 1, 150, false, 5000, "WJ-BTN5", 8},
        {true, TRIG_MANUAL, 120, 1, 150, false, 5000, "WJ-BTN6", 6},
        {true, TRIG_ON_L3_CUT, 120, 1, 150, false, 5000, "WJ-CAM", 10},
        {true, TRIG_ON_L3_RELEASE, 120, 1, 150, false, 5000, "WJ-CAM-OFF", 10},
        {}, {}, {}, {}, {}, {}
    }};
    BootSettings boot{};
};

// ---- Frozen legacy v4 ABI (migration source only; never serialized) ----
struct VactrolSettingsV4 {
    bool     auto_mode;
    uint16_t min_clamp;
    uint16_t max_clamp;
    float    slew_per_ms;
    uint16_t manual_value;
    uint8_t  ave5_pot;
};

struct FxRelaySettingsV4 {
    bool enabled;
    uint8_t trigger;
    uint16_t press_length_ms;
    uint8_t press_count;
    uint16_t press_gap_ms;
    bool clock_enable;
    uint16_t clock_interval_ms;  // v4 width; zero-extended on migration
    char name[12];
    uint8_t ave5_button;
};

struct CalibrationConfigV4 {
    float D_min;
    float D_max;
    float hysteresis;
    float gamma_exponent;
    float slew_rate_limit;
    float breathing_depth_M;
    uint8_t pwm_min_clamp;
    uint8_t pwm_max_clamp;
    bool touch_inverted;
    float variance_threshold_cm;
    uint32_t stationary_lock_time_ms;
    float agc_epsilon;
    size_t agc_window_size;
    uint32_t button_debounce_ms;
    uint32_t multiclick_window_ms;
    uint32_t long_press_ms;
    float breath_threshold;
    uint32_t auto_trigger_cooldown_ms;
    VactrolSettingsV4 vactrol[6];
    FxRelaySettingsV4 fx[8];
    BootSettings boot;
    float pi_zone_far_cm;
    float pi_zone_near_cm;
    char wifi_ssid[32];
    char wifi_password[64];
    uint16_t telemetry_rate_hz;
    uint8_t config_version;
};

inline constexpr std::array<int, 6> LEGACY_VACTROL_PINS{{13, 14, 25, 26, 27, 33}};
inline constexpr std::array<int, 8> LEGACY_RELAY_PINS{{4, 18, 19, 21, 22, 23, 32, 15}};

// ---- API ----
bool configurationValidate(const CalibrationConfig& cfg, const char** error);
size_t configurationEncodedSize(const CalibrationConfig& cfg);
// Serializes the v5 envelope; returns bytes written, or 0 when cfg is
// invalid, out is null, or cap is too small.
size_t configurationEncode(const CalibrationConfig& cfg, uint8_t* out, size_t cap);

enum class LoadStatus { Ok, Migrated, Invalid };
struct LoadResult {
    LoadStatus status;
    const char* error;
};
// Strict v5 decode: on any Invalid result 'out' is left completely unchanged.
// Never returns Migrated: legacy v4 images are read by the NVS glue as raw
// CalibrationConfigV4 and converted via configurationMigrateFromV4().
LoadResult configurationDecode(const uint8_t* data, size_t len, CalibrationConfig& out);
// Field-by-field v4 -> v5 migration (agc_window_size forced into 10..200,
// default 40 otherwise). Caller must configurationValidate() afterwards.
void configurationMigrateFromV4(const CalibrationConfigV4& legacy, CalibrationConfig& out);

// High-level NVS blob loader: decodes v5 APP5 envelope, or if raw 580-byte v4
// blob is detected, migrates it to v5 (status Migrated). On error, returns Invalid
// and leaves 'out' untouched.
LoadResult loadConfigurationBlob(const uint8_t* data, size_t len, CalibrationConfig& out);

}  // namespace apparatus
