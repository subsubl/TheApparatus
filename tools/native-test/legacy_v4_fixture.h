#pragma once
// tools/native-test/legacy_v4_fixture.h
//
// Deterministic, byte-exact legacy v4 NVS image builder for host tests.
// Reproduces the raw 580-byte CalibrationConfigV4 image the on-device glue
// used to store under namespace apparatus, key calib, before the APP5
// envelope existed. Little-endian throughout; every bool is a single 0/1
// byte. Xtensa-confirmed offsets (total 580 bytes):
//   D_min@0 D_max@4 hysteresis@8 gamma@12 slew@16 breathing_depth@20
//   pwm_min@24 pwm_max@25 touch_inverted@26 pad@27 variance@28
//   stationary@32 agc_epsilon@36 agc_window(u32)@40 debounce@44
//   multiclick@48 long_press@52 breath_threshold@56 cooldown@60
//   vactrol[6]@64 (16B rows: auto@0 min@2 max@4 slew@8 manual@12 pot@14)
//   fx[8]@160 (26B rows: en@0 trig@1 len@2 cnt@4 gap@6 cen@8 cint@10
//   name@12..23 ave5@24 pad@25)
//   boot@368 (102B: enabled@0 start_delay@2 step_count@4 pad@5 steps@6+8i)
//   pi_zone_far@472 pi_zone_near@476 ssid@480 (32B) password@512 (64B)
//   telemetry@576 (u16) config_version@578 (u8) pad@579
//
// Accessor strategy (chosen over struct memcpy because host size_t is 8
// bytes while Xtensa size_t is 4): parseV4Image() fills a host
// CalibrationConfigV4 field-by-field from the bytes and enforces strict
// 0/1 bool bytes so a corrupted bool byte is detectable; fixtureV4()
// caches the parsed canonical image. Expected scenario values are
// declared constexpr below for the codec tests.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Configuration.h"

namespace apparatus_test {

// ---- frozen v4 image offsets ----
inline constexpr size_t kV4OffDMin = 0;
inline constexpr size_t kV4OffDMax = 4;
inline constexpr size_t kV4OffHysteresis = 8;
inline constexpr size_t kV4OffGamma = 12;
inline constexpr size_t kV4OffSlew = 16;
inline constexpr size_t kV4OffBreathDepth = 20;
inline constexpr size_t kV4OffPwmMin = 24;
inline constexpr size_t kV4OffPwmMax = 25;
inline constexpr size_t kV4OffTouchInverted = 26;
inline constexpr size_t kV4OffVariance = 28;
inline constexpr size_t kV4OffStationary = 32;
inline constexpr size_t kV4OffAgcEpsilon = 36;
inline constexpr size_t kV4OffAgcWindow = 40;  // Xtensa size_t: stored as u32
inline constexpr size_t kV4OffDebounce = 44;
inline constexpr size_t kV4OffMulticlick = 48;
inline constexpr size_t kV4OffLongPress = 52;
inline constexpr size_t kV4OffBreathThreshold = 56;
inline constexpr size_t kV4OffCooldown = 60;
inline constexpr size_t kV4OffVactrolBase = 64;  // 6 rows x 16B
inline constexpr size_t kV4VactrolStride = 16;
inline constexpr size_t kV4OffFxBase = 160;      // 8 rows x 26B
inline constexpr size_t kV4FxStride = 26;
inline constexpr size_t kV4OffBoot = 368;        // 102B total
inline constexpr size_t kV4OffZoneFar = 472;
inline constexpr size_t kV4OffZoneNear = 476;
inline constexpr size_t kV4OffSsid = 480;        // 32B
inline constexpr size_t kV4OffPassword = 512;    // 64B
inline constexpr size_t kV4OffTelemetry = 576;   // u16
inline constexpr size_t kV4OffVersion = 578;     // u8
inline constexpr size_t kV4ImageSize = 580;

// ---- expected scenario values (constexpr, consumed by the tests) ----
inline constexpr float kExpDMin = 80.0f;
inline constexpr float kExpDMax = 420.0f;
inline constexpr float kExpHysteresis = 15.0f;
inline constexpr float kExpGamma = 1.9f;
inline constexpr float kExpSlew = 4.0f;
inline constexpr float kExpBreathDepth = 0.35f;
inline constexpr uint8_t kExpPwmMin = 0;
inline constexpr uint8_t kExpPwmMax = 255;
inline constexpr bool kExpTouchInverted = false;
inline constexpr float kExpVariance = 5.0f;
inline constexpr uint32_t kExpStationaryMs = 1500;
inline constexpr float kExpAgcEpsilon = 1e-6f;
inline constexpr uint32_t kExpAgcWindow = 40;
inline constexpr uint32_t kExpDebounceMs = 30;
inline constexpr uint32_t kExpMulticlickMs = 320;
inline constexpr uint32_t kExpLongPressMs = 650;
inline constexpr float kExpBreathThreshold = 0.45f;
inline constexpr uint32_t kExpCooldownMs = 2500;
inline constexpr float kExpZoneFarCm = 340.0f;
inline constexpr float kExpZoneNearCm = 130.0f;
inline constexpr uint16_t kExpTelemetryHz = 25;
inline constexpr uint8_t kExpV4Version = 4;
inline constexpr int kExpVactrolCount = 6;
inline constexpr int kExpRelayCount = 8;
inline constexpr int kExpVactrolPins[6] = {13, 14, 25, 26, 27, 33};
inline constexpr int kExpRelayPins[8] = {4, 18, 19, 21, 22, 23, 32, 15};

inline constexpr bool kExpV0Auto = true;
inline constexpr uint16_t kExpV0Min = 0;
inline constexpr uint16_t kExpV0Max = 1023;
inline constexpr float kExpV0Slew = 2.0f;
inline constexpr uint16_t kExpV0Manual = 0;
inline constexpr uint8_t kExpV0Pot = 1;

inline constexpr bool kExpV2Auto = false;
inline constexpr uint16_t kExpV2Min = 50;
inline constexpr uint16_t kExpV2Max = 900;
inline constexpr float kExpV2Slew = 2.0f;
inline constexpr uint16_t kExpV2Manual = 400;
inline constexpr uint8_t kExpV2Pot = 3;

inline constexpr bool kExpF2Enabled = true;
inline constexpr uint8_t kExpF2Trigger = 4;  // TRIG_ON_L3_CUT
inline constexpr uint16_t kExpF2PressMs = 120;
inline constexpr uint8_t kExpF2Count = 1;
inline constexpr uint16_t kExpF2GapMs = 150;
inline constexpr bool kExpF2ClockEn = false;
inline constexpr uint32_t kExpF2ClockIntervalMs = 5000;  // u16 in v4, zero-extended on migration
inline constexpr const char* kExpF2Name = "WJ-CAM";
inline constexpr uint8_t kExpF2Ave5Button = 10;

inline constexpr bool kExpF7Enabled = true;
inline constexpr uint8_t kExpF7Trigger = 7;  // TRIG_ON_L3_RELEASE
inline constexpr uint16_t kExpF7PressMs = 120;
inline constexpr uint8_t kExpF7Count = 1;
inline constexpr uint16_t kExpF7GapMs = 150;
inline constexpr bool kExpF7ClockEn = false;
inline constexpr uint32_t kExpF7ClockIntervalMs = 5000;
inline constexpr const char* kExpF7Name = "WJ-CAM-OFF";
inline constexpr uint8_t kExpF7Ave5Button = 10;

inline constexpr bool kExpBootEnabled = true;
inline constexpr uint16_t kExpBootStartDelayMs = 4000;
inline constexpr uint8_t kExpBootStepCount = 2;
inline constexpr uint8_t kExpStep0Relay = 0;
inline constexpr uint8_t kExpStep0Presses = 1;
inline constexpr uint16_t kExpStep0LengthMs = 400;
inline constexpr uint16_t kExpStep0GapMs = 150;
inline constexpr uint16_t kExpStep0WaitMs = 2000;
inline constexpr uint8_t kExpStep1Relay = 1;
inline constexpr uint8_t kExpStep1Presses = 1;
inline constexpr uint16_t kExpStep1LengthMs = 120;
inline constexpr uint16_t kExpStep1GapMs = 150;
inline constexpr uint16_t kExpStep1WaitMs = 500;

inline constexpr const char* kExpSsid = "TestAP_2G";
inline constexpr const char* kExpPassword = "pw1234567890123";

// ---- little-endian byte image writer ----
struct V4Image {
    std::array<unsigned char, kV4ImageSize> b{};
    void u8(size_t o, uint8_t v) { b[o] = v; }
    void u16(size_t o, uint16_t v) {
        b[o] = static_cast<unsigned char>(v & 0xFFu);
        b[o + 1] = static_cast<unsigned char>(v >> 8);
    }
    void u32(size_t o, uint32_t v) {
        for (size_t i = 0; i < 4; ++i)
            b[o + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFFu);
    }
    void f32(size_t o, float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, 4);
        u32(o, bits);
    }
    void str(size_t o, const char* s, size_t cap) {
        size_t i = 0;
        for (; s[i] != 0 && i < cap; ++i) b[o + i] = static_cast<unsigned char>(s[i]);
        for (; i < cap; ++i) b[o + i] = 0;
    }
};

inline std::array<unsigned char, kV4ImageSize> buildLegacyV4Blob() {
    V4Image w;
    // header scalars @0..63
    w.f32(kV4OffDMin, kExpDMin);
    w.f32(kV4OffDMax, kExpDMax);
    w.f32(kV4OffHysteresis, kExpHysteresis);
    w.f32(kV4OffGamma, kExpGamma);
    w.f32(kV4OffSlew, kExpSlew);
    w.f32(kV4OffBreathDepth, kExpBreathDepth);
    w.u8(kV4OffPwmMin, kExpPwmMin);
    w.u8(kV4OffPwmMax, kExpPwmMax);
    w.u8(kV4OffTouchInverted, kExpTouchInverted ? 1 : 0);
    w.u8(27, 0);  // struct padding
    w.f32(kV4OffVariance, kExpVariance);
    w.u32(kV4OffStationary, kExpStationaryMs);
    w.f32(kV4OffAgcEpsilon, kExpAgcEpsilon);
    w.u32(kV4OffAgcWindow, kExpAgcWindow);
    w.u32(kV4OffDebounce, kExpDebounceMs);
    w.u32(kV4OffMulticlick, kExpMulticlickMs);
    w.u32(kV4OffLongPress, kExpLongPressMs);
    w.f32(kV4OffBreathThreshold, kExpBreathThreshold);
    w.u32(kV4OffCooldown, kExpCooldownMs);
    // vactrol[6] @64, 16B rows
    auto vactrolRow = [&](size_t row, bool autoMode, uint16_t mn, uint16_t mx, float slew,
                          uint16_t man, uint8_t pot) {
        const size_t base = kV4OffVactrolBase + row * kV4VactrolStride;
        w.u8(base, autoMode ? 1 : 0);
        w.u16(base + 2, mn);
        w.u16(base + 4, mx);
        w.f32(base + 8, slew);
        w.u16(base + 12, man);
        w.u8(base + 14, pot);
        w.u8(base + 15, 0);  // padding
    };
    vactrolRow(0, kExpV0Auto, kExpV0Min, kExpV0Max, kExpV0Slew, kExpV0Manual, kExpV0Pot);
    vactrolRow(1, false, 0, 1023, 2.0f, 0, 0);
    vactrolRow(2, kExpV2Auto, kExpV2Min, kExpV2Max, kExpV2Slew, kExpV2Manual, kExpV2Pot);
    for (size_t i = 3; i < 6; ++i) vactrolRow(i, false, 0, 1023, 2.0f, 0, 0);
    // fx[8] @160, 26B rows
    auto fxRow = [&](size_t row, bool en, uint8_t trig, uint16_t len, uint8_t cnt, uint16_t gap,
                     bool cen, uint16_t cint, const char* name, uint8_t ave5) {
        const size_t base = kV4OffFxBase + row * kV4FxStride;
        w.u8(base, en ? 1 : 0);
        w.u8(base + 1, trig);
        w.u16(base + 2, len);
        w.u8(base + 4, cnt);
        w.u16(base + 6, gap);
        w.u8(base + 8, cen ? 1 : 0);
        w.u16(base + 10, cint);
        w.str(base + 12, name, 12);
        w.u8(base + 24, ave5);
        w.u8(base + 25, 0);  // padding
    };
    for (size_t i = 0; i < 8; ++i) fxRow(i, false, 0, 0, 0, 0, false, 0, "", 0);
    fxRow(2, kExpF2Enabled, kExpF2Trigger, kExpF2PressMs, kExpF2Count, kExpF2GapMs, kExpF2ClockEn,
          static_cast<uint16_t>(kExpF2ClockIntervalMs), kExpF2Name, kExpF2Ave5Button);
    fxRow(7, kExpF7Enabled, kExpF7Trigger, kExpF7PressMs, kExpF7Count, kExpF7GapMs, kExpF7ClockEn,
          static_cast<uint16_t>(kExpF7ClockIntervalMs), kExpF7Name, kExpF7Ave5Button);
    // boot @368, 102B
    w.u8(kV4OffBoot, kExpBootEnabled ? 1 : 0);
    w.u16(kV4OffBoot + 2, kExpBootStartDelayMs);
    w.u8(kV4OffBoot + 4, kExpBootStepCount);
    w.u8(kV4OffBoot + 5, 0);  // padding before the steps array
    auto bootStep = [&](size_t row, uint8_t relay, uint8_t presses, uint16_t len, uint16_t gap,
                        uint16_t wait) {
        const size_t base = kV4OffBoot + 6 + row * 8;
        w.u8(base, relay);
        w.u8(base + 1, presses);
        w.u16(base + 2, len);
        w.u16(base + 4, gap);
        w.u16(base + 6, wait);
    };
    bootStep(0, kExpStep0Relay, kExpStep0Presses, kExpStep0LengthMs, kExpStep0GapMs,
             kExpStep0WaitMs);
    bootStep(1, kExpStep1Relay, kExpStep1Presses, kExpStep1LengthMs, kExpStep1GapMs,
             kExpStep1WaitMs);
    // boot steps 2..11 and the trailing boot padding stay zero
    // zones, wifi and trailer
    w.f32(kV4OffZoneFar, kExpZoneFarCm);
    w.f32(kV4OffZoneNear, kExpZoneNearCm);
    w.str(kV4OffSsid, kExpSsid, 32);
    w.str(kV4OffPassword, kExpPassword, 64);
    w.u16(kV4OffTelemetry, kExpTelemetryHz);
    w.u8(kV4OffVersion, kExpV4Version);
    w.u8(579, 0);  // tail padding
    return w.b;
}

inline const std::array<unsigned char, kV4ImageSize>& legacyV4Blob() {
    static const std::array<unsigned char, kV4ImageSize> kBlob = buildLegacyV4Blob();
    return kBlob;
}

// ---- byte accessor helpers (little-endian) ----
inline uint8_t blobU8(const std::array<unsigned char, kV4ImageSize>& b, size_t o) { return b[o]; }
inline uint16_t blobU16(const std::array<unsigned char, kV4ImageSize>& b, size_t o) {
    return static_cast<uint16_t>(b[o] | (static_cast<uint16_t>(b[o + 1]) << 8));
}
inline uint32_t blobU32(const std::array<unsigned char, kV4ImageSize>& b, size_t o) {
    return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
           (static_cast<uint32_t>(b[o + 2]) << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
}
inline float blobF32(const std::array<unsigned char, kV4ImageSize>& b, size_t o) {
    const uint32_t bits = blobU32(b, o);
    float v = 0.0f;
    std::memcpy(&v, &bits, 4);
    return v;
}

// Fills a host CalibrationConfigV4 field-by-field from the byte image
// (never memcpy: host size_t width differs from Xtensa). Strict 0/1 bools.
// Returns nullptr on success, else a static error literal.
inline const char* parseV4Image(const std::array<unsigned char, kV4ImageSize>& b,
                                apparatus::CalibrationConfigV4& o) {
    bool ok = true;
    auto boolAt = [&](size_t off, bool* dst) {
        if (b[off] > 1) {
            ok = false;
            *dst = false;
            return;
        }
        *dst = (b[off] != 0);
    };
    o.D_min = blobF32(b, kV4OffDMin);
    o.D_max = blobF32(b, kV4OffDMax);
    o.hysteresis = blobF32(b, kV4OffHysteresis);
    o.gamma_exponent = blobF32(b, kV4OffGamma);
    o.slew_rate_limit = blobF32(b, kV4OffSlew);
    o.breathing_depth_M = blobF32(b, kV4OffBreathDepth);
    o.pwm_min_clamp = blobU8(b, kV4OffPwmMin);
    o.pwm_max_clamp = blobU8(b, kV4OffPwmMax);
    boolAt(kV4OffTouchInverted, &o.touch_inverted);
    o.variance_threshold_cm = blobF32(b, kV4OffVariance);
    o.stationary_lock_time_ms = blobU32(b, kV4OffStationary);
    o.agc_epsilon = blobF32(b, kV4OffAgcEpsilon);
    o.agc_window_size = static_cast<size_t>(blobU32(b, kV4OffAgcWindow));
    o.button_debounce_ms = blobU32(b, kV4OffDebounce);
    o.multiclick_window_ms = blobU32(b, kV4OffMulticlick);
    o.long_press_ms = blobU32(b, kV4OffLongPress);
    o.breath_threshold = blobF32(b, kV4OffBreathThreshold);
    o.auto_trigger_cooldown_ms = blobU32(b, kV4OffCooldown);
    for (size_t i = 0; i < 6; ++i) {
        const size_t base = kV4OffVactrolBase + i * kV4VactrolStride;
        apparatus::VactrolSettings s{};
        boolAt(base, &s.auto_mode);
        s.min_clamp = blobU16(b, base + 2);
        s.max_clamp = blobU16(b, base + 4);
        s.slew_per_ms = blobF32(b, base + 8);
        s.manual_value = blobU16(b, base + 12);
        s.ave5_pot = blobU8(b, base + 14);
        o.vactrol[i] = s;
    }
    for (size_t i = 0; i < 8; ++i) {
        const size_t base = kV4OffFxBase + i * kV4FxStride;
        apparatus::FxRelaySettingsV4 s{};
        boolAt(base, &s.enabled);
        s.trigger = blobU8(b, base + 1);
        s.press_length_ms = blobU16(b, base + 2);
        s.press_count = blobU8(b, base + 4);
        s.press_gap_ms = blobU16(b, base + 6);
        boolAt(base + 8, &s.clock_enable);
        s.clock_interval_ms = blobU16(b, base + 10);
        for (size_t k = 0; k < 12; ++k) s.name[k] = static_cast<char>(blobU8(b, base + 12 + k));
        s.ave5_button = blobU8(b, base + 24);
        o.fx[i] = s;
    }
    boolAt(kV4OffBoot, &o.boot.enabled);
    o.boot.start_delay_ms = blobU16(b, kV4OffBoot + 2);
    o.boot.step_count = blobU8(b, kV4OffBoot + 4);
    for (size_t i = 0; i < 12; ++i) {
        const size_t base = kV4OffBoot + 6 + i * 8;
        apparatus::BootStep s{};
        s.relay = blobU8(b, base);
        s.presses = blobU8(b, base + 1);
        s.length_ms = blobU16(b, base + 2);
        s.gap_ms = blobU16(b, base + 4);
        s.wait_after_ms = blobU16(b, base + 6);
        o.boot.steps[i] = s;
    }
    o.pi_zone_far_cm = blobF32(b, kV4OffZoneFar);
    o.pi_zone_near_cm = blobF32(b, kV4OffZoneNear);
    for (size_t k = 0; k < 32; ++k) o.wifi_ssid[k] = static_cast<char>(blobU8(b, kV4OffSsid + k));
    for (size_t k = 0; k < 64; ++k)
        o.wifi_password[k] = static_cast<char>(blobU8(b, kV4OffPassword + k));
    o.telemetry_rate_hz = blobU16(b, kV4OffTelemetry);
    o.config_version = blobU8(b, kV4OffVersion);
    if (!ok) return "v4 image has a bool byte that is not 0/1";
    return nullptr;
}

// Canonical parsed fixture image (always strict-valid).
inline const apparatus::CalibrationConfigV4& fixtureV4() {
    static const apparatus::CalibrationConfigV4 v = [] {
        apparatus::CalibrationConfigV4 x{};
        parseV4Image(legacyV4Blob(), x);
        return x;
    }();
    return v;
}

}  // namespace apparatus_test
