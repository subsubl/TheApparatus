// tools/native-test/config_codec_test.cpp
//
// Host-side tests for the Configuration codec (module 02a) and the output
// allocator (module 01) of subsubl/TheApparatus. Portable C++17: no Arduino,
// no exceptions, no RTTI, no heap beyond static fixture caching.
// Build (single command line):
// g++ -std=c++17 -fno-exceptions -fno-rtti -Iinclude -Itools/native-test src/OutputAllocation.cpp src/Configuration.cpp tools/native-test/config_codec_test.cpp -o config-test
//
// Scenario groups:
//   1 fresh defaults validate
//   2 v5 encode -> decode roundtrip (every scalar, counts, all 28 pin
//     cells, vactrol[0]/[2], fx[0]/[2]/[7], boot, wifi, telemetry)
//   3 flipped payload byte -> Invalid, out-config unchanged
//   4 truncated (len-1) and payload_len+2 appended -> Invalid, unchanged
//   5 magic APP4 -> Invalid
//   6 envelope version 4 -> Invalid; raw v4 image into the v5 decoder -> Invalid
//   7 580-byte v4 fixture -> Migrated, every field verified, validates
//   8 addOutput(Relay) on a migrated copy succeeds and still validates
//   9 v4 bool byte 2 at vactrol[2].auto_mode -> Invalid
//  10 v4 D_min=500 >= D_max=420 -> Invalid after migration validation
//  11 invalid config (reserved pin 12) refused by encode, buffer untouched
//  12 cap = needed-1 -> encode returns 0
//  13 declared payload_len larger than the actual buffer -> Invalid

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Configuration.h"
#include "OutputAllocation.h"
#include "legacy_v4_fixture.h"

using namespace apparatus;
using namespace apparatus_test;

namespace {

// ---- v5 envelope facts (module 02a) ----
constexpr size_t kEnvHeaderSize = 12;  // magic(4) ver(1) flags(1) payload_len(2) crc32(4)
constexpr size_t kEnvPayloadLenOff = 6;

// ---- tiny assertion framework: failures print field names ----
int g_checks = 0;
int g_failures = 0;

void expectTrue(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void expectInt(long long got, long long want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s (got %lld, want %lld)\n", what, got, want);
    }
}

// The codec copies float bits verbatim, so exact equality is the contract.
void expectFloat(float got, float want, const char* what) {
    ++g_checks;
    if (!(got == want)) {
        ++g_failures;
        std::printf("FAIL: %s (float value mismatch)\n", what);
    }
}

void expectStr(const char* got, const char* want, const char* what) {
    ++g_checks;
    if (std::strcmp(got, want) != 0) {
        ++g_failures;
        std::printf("FAIL: %s (string mismatch)\n", what);
    }
}

// ---- row helpers ----
bool vactrolEq(const VactrolSettings& a, const VactrolSettings& b) {
    return a.auto_mode == b.auto_mode && a.min_clamp == b.min_clamp &&
           a.max_clamp == b.max_clamp && a.slew_per_ms == b.slew_per_ms &&
           a.manual_value == b.manual_value && a.ave5_pot == b.ave5_pot;
}

bool fxRelayEq(const FxRelaySettings& a, const FxRelaySettings& b) {
    return a.enabled == b.enabled && a.trigger == b.trigger &&
           a.press_length_ms == b.press_length_ms && a.press_count == b.press_count &&
           a.press_gap_ms == b.press_gap_ms && a.clock_enable == b.clock_enable &&
           a.clock_interval_ms == b.clock_interval_ms &&
           std::memcmp(a.name, b.name, sizeof(a.name)) == 0 && a.ave5_button == b.ave5_button;
}

bool vactrolZeroed(const VactrolSettings& s) { return vactrolEq(s, VactrolSettings{}); }
bool fxZeroed(const FxRelaySettings& s) { return fxRelayEq(s, FxRelaySettings{}); }

// Full structural equality: the decode contract is that any Invalid leaves
// the out-config unchanged, so compare every field.
bool sameConfig(const CalibrationConfig& a, const CalibrationConfig& b) {
    if (a.D_min != b.D_min || a.D_max != b.D_max || a.hysteresis != b.hysteresis ||
        a.gamma_exponent != b.gamma_exponent || a.slew_rate_limit != b.slew_rate_limit ||
        a.breathing_depth_M != b.breathing_depth_M)
        return false;
    if (a.pwm_min_clamp != b.pwm_min_clamp || a.pwm_max_clamp != b.pwm_max_clamp ||
        a.touch_inverted != b.touch_inverted)
        return false;
    if (a.variance_threshold_cm != b.variance_threshold_cm ||
        a.stationary_lock_time_ms != b.stationary_lock_time_ms ||
        a.agc_epsilon != b.agc_epsilon || a.agc_window_size != b.agc_window_size ||
        a.button_debounce_ms != b.button_debounce_ms ||
        a.multiclick_window_ms != b.multiclick_window_ms || a.long_press_ms != b.long_press_ms ||
        a.breath_threshold != b.breath_threshold ||
        a.auto_trigger_cooldown_ms != b.auto_trigger_cooldown_ms ||
        a.pi_zone_far_cm != b.pi_zone_far_cm || a.pi_zone_near_cm != b.pi_zone_near_cm)
        return false;
    if (std::memcmp(a.wifi_ssid, b.wifi_ssid, sizeof(a.wifi_ssid)) != 0) return false;
    if (std::memcmp(a.wifi_password, b.wifi_password, sizeof(a.wifi_password)) != 0) return false;
    if (a.telemetry_rate_hz != b.telemetry_rate_hz || a.config_version != b.config_version)
        return false;
    if (a.vactrol_count != b.vactrol_count || a.relay_count != b.relay_count) return false;
    if (a.layout.vactrol_count != b.layout.vactrol_count ||
        a.layout.relay_count != b.layout.relay_count)
        return false;
    for (int i = 0; i < 14; ++i) {
        if (a.layout.vactrol_pins[i] != b.layout.vactrol_pins[i]) return false;
        if (a.layout.relay_pins[i] != b.layout.relay_pins[i]) return false;
    }
    for (int i = 0; i < 14; ++i) {
        if (!vactrolEq(a.vactrol[i], b.vactrol[i])) return false;
        if (!fxRelayEq(a.fx[i], b.fx[i])) return false;
    }
    if (a.boot.enabled != b.boot.enabled || a.boot.start_delay_ms != b.boot.start_delay_ms ||
        a.boot.step_count != b.boot.step_count)
        return false;
    for (int i = 0; i < 12; ++i) {
        const BootStep& x = a.boot.steps[i];
        const BootStep& y = b.boot.steps[i];
        if (x.relay != y.relay || x.presses != y.presses || x.length_ms != y.length_ms ||
            x.gap_ms != y.gap_ms || x.wait_after_ms != y.wait_after_ms)
            return false;
    }
    return true;
}

void copyStr(char* dst, size_t cap, const char* src) {
    size_t i = 0;
    for (; src[i] != 0 && i + 1 < cap; ++i) dst[i] = src[i];
    dst[i] = 0;
    for (++i; i < cap; ++i) dst[i] = 0;
}

// Mirror of the NVS-glue dispatch: a raw image of exactly the frozen v4
// size carries no envelope, so it is strict-parsed, migrated, then validated
// (status Migrated); everything else goes through the strict v5 decoder.
LoadResult loadConfigurationBlob(const uint8_t* data, size_t len, CalibrationConfig& out) {
    if (data != nullptr && len == kV4ImageSize) {
        std::array<unsigned char, kV4ImageSize> raw{};
        std::memcpy(raw.data(), data, kV4ImageSize);
        CalibrationConfigV4 legacy{};
        const char* perr = parseV4Image(raw, legacy);
        if (perr != nullptr) return {LoadStatus::Invalid, perr};
        CalibrationConfig staged{};
        configurationMigrateFromV4(legacy, staged);
        const char* verr = nullptr;
        if (!configurationValidate(staged, &verr)) return {LoadStatus::Invalid, verr};
        out = staged;   // commit only on success => out unchanged on Invalid
        return {LoadStatus::Migrated, nullptr};
    }
    return configurationDecode(data, len, out);
}

// Roundtrip source: the fixture scenario on a 3-vactrol / 8-relay rig with
// the legacy pin topology (a pattern the migrated fixture proves valid).
CalibrationConfig makeRoundtripSource() {
    CalibrationConfig c{};
    c.D_min = kExpDMin;
    c.D_max = kExpDMax;
    c.hysteresis = kExpHysteresis;
    c.gamma_exponent = kExpGamma;
    c.slew_rate_limit = kExpSlew;
    c.breathing_depth_M = kExpBreathDepth;
    c.pwm_min_clamp = kExpPwmMin;
    c.pwm_max_clamp = kExpPwmMax;
    c.touch_inverted = kExpTouchInverted;
    c.variance_threshold_cm = kExpVariance;
    c.stationary_lock_time_ms = kExpStationaryMs;
    c.agc_epsilon = kExpAgcEpsilon;
    c.agc_window_size = static_cast<uint16_t>(kExpAgcWindow);
    c.button_debounce_ms = kExpDebounceMs;
    c.multiclick_window_ms = kExpMulticlickMs;
    c.long_press_ms = kExpLongPressMs;
    c.breath_threshold = kExpBreathThreshold;
    c.auto_trigger_cooldown_ms = kExpCooldownMs;
    c.pi_zone_far_cm = kExpZoneFarCm;
    c.pi_zone_near_cm = kExpZoneNearCm;
    c.vactrol_count = 3;
    c.relay_count = 8;
    c.layout.vactrol_count = 3;
    c.layout.relay_count = 8;
    for (int i = 0; i < 14; ++i) {
        c.layout.vactrol_pins[i] = (i < 3) ? kExpVactrolPins[i] : -1;
        c.layout.relay_pins[i] = (i < 8) ? kExpRelayPins[i] : -1;
    }
    for (int i = 0; i < 14; ++i) c.vactrol[i] = VactrolSettings{false, 0, 1023, 2.0f, 0, 0};
    c.vactrol[0] = VactrolSettings{kExpV0Auto, kExpV0Min, kExpV0Max, kExpV0Slew, kExpV0Manual,
                                   kExpV0Pot};
    c.vactrol[2] = VactrolSettings{kExpV2Auto, kExpV2Min, kExpV2Max, kExpV2Slew, kExpV2Manual,
                                   kExpV2Pot};
    for (int i = 0; i < 14; ++i)
        c.fx[i] = FxRelaySettings{false, TRIG_MANUAL, 0, 0, 0, false, 0, "", 0};
    c.fx[0] = FxRelaySettings{true, TRIG_MANUAL, 120, 1, 150, false, 5000, "WJ-BTN1", 1};
    c.fx[2] = FxRelaySettings{kExpF2Enabled, kExpF2Trigger, kExpF2PressMs, kExpF2Count,
                              kExpF2GapMs, kExpF2ClockEn, kExpF2ClockIntervalMs, "WJ-CAM",
                              kExpF2Ave5Button};
    c.fx[7] = FxRelaySettings{kExpF7Enabled, kExpF7Trigger, kExpF7PressMs, kExpF7Count,
                              kExpF7GapMs, kExpF7ClockEn, kExpF7ClockIntervalMs, "WJ-CAM-OFF",
                              kExpF7Ave5Button};
    c.boot.enabled = kExpBootEnabled;
    c.boot.start_delay_ms = kExpBootStartDelayMs;
    c.boot.step_count = kExpBootStepCount;
    for (int i = 0; i < 12; ++i) c.boot.steps[i] = BootStep{0, 0, 0, 0, 0};
    c.boot.steps[0] = BootStep{kExpStep0Relay, kExpStep0Presses, kExpStep0LengthMs,
                               kExpStep0GapMs, kExpStep0WaitMs};
    c.boot.steps[1] = BootStep{kExpStep1Relay, kExpStep1Presses, kExpStep1LengthMs,
                               kExpStep1GapMs, kExpStep1WaitMs};
    copyStr(c.wifi_ssid, sizeof(c.wifi_ssid), kExpSsid);
    copyStr(c.wifi_password, sizeof(c.wifi_password), kExpPassword);
    c.telemetry_rate_hz = kExpTelemetryHz;
    return c;
}

}  // namespace

int main() {
    const char* err = nullptr;
    char label[96];
    CalibrationConfig t2decoded{};
    CalibrationConfig t7migrated{};
    std::array<uint8_t, 1024> env{};
    size_t envLen = 0;

    // ---- (1) fresh defaults validate ----
    {
        CalibrationConfig fresh{};
        err = nullptr;
        expectTrue(configurationValidate(fresh, &err),
                   "t1 fresh defaults configurationValidate == true");
        expectTrue(err == nullptr, "t1 fresh defaults error == nullptr");
    }

    // ---- (2) v5 encode -> decode roundtrip ----
    {
        CalibrationConfig src = makeRoundtripSource();
        err = nullptr;
        expectTrue(configurationValidate(src, &err), "t2 roundtrip source validates");
        const size_t need = configurationEncodedSize(src);
        expectInt(static_cast<long long>(need), 750,
                  "t2 encoded size 750 (12 header + 722 fixed + 2*8 boot steps)");
        envLen = configurationEncode(src, env.data(), env.size());
        expectInt(static_cast<long long>(envLen), static_cast<long long>(need),
                  "t2 encode length");
        LoadResult lr = configurationDecode(env.data(), envLen, t2decoded);
        expectTrue(lr.status == LoadStatus::Ok, "t2 decode status Ok");
        expectTrue(lr.error == nullptr, "t2 decode error == nullptr");
        const CalibrationConfig& d = t2decoded;
        // every scalar
        expectFloat(d.D_min, src.D_min, "t2 D_min");
        expectFloat(d.D_max, src.D_max, "t2 D_max");
        expectFloat(d.hysteresis, src.hysteresis, "t2 hysteresis");
        expectFloat(d.gamma_exponent, src.gamma_exponent, "t2 gamma_exponent");
        expectFloat(d.slew_rate_limit, src.slew_rate_limit, "t2 slew_rate_limit");
        expectFloat(d.breathing_depth_M, src.breathing_depth_M, "t2 breathing_depth_M");
        expectInt(d.pwm_min_clamp, src.pwm_min_clamp, "t2 pwm_min_clamp");
        expectInt(d.pwm_max_clamp, src.pwm_max_clamp, "t2 pwm_max_clamp");
        expectTrue(d.touch_inverted == src.touch_inverted, "t2 touch_inverted");
        expectFloat(d.variance_threshold_cm, src.variance_threshold_cm,
                    "t2 variance_threshold_cm");
        expectInt(d.stationary_lock_time_ms, src.stationary_lock_time_ms,
                  "t2 stationary_lock_time_ms");
        expectFloat(d.agc_epsilon, src.agc_epsilon, "t2 agc_epsilon");
        expectInt(d.agc_window_size, src.agc_window_size, "t2 agc_window_size");
        expectInt(d.button_debounce_ms, src.button_debounce_ms, "t2 button_debounce_ms");
        expectInt(d.multiclick_window_ms, src.multiclick_window_ms, "t2 multiclick_window_ms");
        expectInt(d.long_press_ms, src.long_press_ms, "t2 long_press_ms");
        expectFloat(d.breath_threshold, src.breath_threshold, "t2 breath_threshold");
        expectInt(d.auto_trigger_cooldown_ms, src.auto_trigger_cooldown_ms,
                  "t2 auto_trigger_cooldown_ms");
        expectFloat(d.pi_zone_far_cm, src.pi_zone_far_cm, "t2 pi_zone_far_cm");
        expectFloat(d.pi_zone_near_cm, src.pi_zone_near_cm, "t2 pi_zone_near_cm");
        expectInt(d.telemetry_rate_hz, src.telemetry_rate_hz, "t2 telemetry_rate_hz");
        expectInt(d.config_version, src.config_version, "t2 config_version");
        // counts
        expectInt(d.vactrol_count, src.vactrol_count, "t2 vactrol_count");
        expectInt(d.relay_count, src.relay_count, "t2 relay_count");
        // all 28 active+inactive pin cells
        for (int i = 0; i < 14; ++i) {
            std::snprintf(label, sizeof(label), "t2 layout.vactrol_pins[%d]", i);
            expectInt(d.layout.vactrol_pins[i], src.layout.vactrol_pins[i], label);
            std::snprintf(label, sizeof(label), "t2 layout.relay_pins[%d]", i);
            expectInt(d.layout.relay_pins[i], src.layout.relay_pins[i], label);
        }
        expectInt(d.layout.vactrol_count, src.layout.vactrol_count, "t2 layout.vactrol_count");
        expectInt(d.layout.relay_count, src.layout.relay_count, "t2 layout.relay_count");
        // vactrol rows: exact for active rows, zeroed for inactive rows
        for (int i = 0; i < 14; ++i) {
            std::snprintf(label, sizeof(label), "t2 vactrol[%d] row roundtrip", i);
            if (i < src.vactrol_count)
                expectTrue(vactrolEq(d.vactrol[i], src.vactrol[i]), label);
            else
                expectTrue(vactrolZeroed(d.vactrol[i]), label);
        }
        // explicit vactrol[0]/[2] fields
        expectTrue(d.vactrol[0].auto_mode == kExpV0Auto, "t2 vactrol[0].auto_mode");
        expectInt(d.vactrol[0].min_clamp, kExpV0Min, "t2 vactrol[0].min_clamp");
        expectInt(d.vactrol[0].max_clamp, kExpV0Max, "t2 vactrol[0].max_clamp");
        expectFloat(d.vactrol[0].slew_per_ms, kExpV0Slew, "t2 vactrol[0].slew_per_ms");
        expectInt(d.vactrol[0].manual_value, kExpV0Manual, "t2 vactrol[0].manual_value");
        expectInt(d.vactrol[0].ave5_pot, kExpV0Pot, "t2 vactrol[0].ave5_pot");
        expectTrue(d.vactrol[2].auto_mode == kExpV2Auto, "t2 vactrol[2].auto_mode");
        expectInt(d.vactrol[2].min_clamp, kExpV2Min, "t2 vactrol[2].min_clamp");
        expectInt(d.vactrol[2].max_clamp, kExpV2Max, "t2 vactrol[2].max_clamp");
        expectFloat(d.vactrol[2].slew_per_ms, kExpV2Slew, "t2 vactrol[2].slew_per_ms");
        expectInt(d.vactrol[2].manual_value, kExpV2Manual, "t2 vactrol[2].manual_value");
        expectInt(d.vactrol[2].ave5_pot, kExpV2Pot, "t2 vactrol[2].ave5_pot");
        // fx rows: exact for active rows, zeroed for inactive rows
        for (int i = 0; i < 14; ++i) {
            std::snprintf(label, sizeof(label), "t2 fx[%d] row roundtrip", i);
            if (i < src.relay_count)
                expectTrue(fxRelayEq(d.fx[i], src.fx[i]), label);
            else
                expectTrue(fxZeroed(d.fx[i]), label);
        }
        // explicit fx[0]/fx[2]/fx[7] fields
        expectTrue(d.fx[0].enabled, "t2 fx[0].enabled");
        expectInt(d.fx[0].trigger, TRIG_MANUAL, "t2 fx[0].trigger");
        expectInt(d.fx[0].press_length_ms, 120, "t2 fx[0].press_length_ms");
        expectInt(d.fx[0].press_count, 1, "t2 fx[0].press_count");
        expectInt(d.fx[0].press_gap_ms, 150, "t2 fx[0].press_gap_ms");
        expectTrue(!d.fx[0].clock_enable, "t2 fx[0].clock_enable");
        expectInt(d.fx[0].clock_interval_ms, 5000, "t2 fx[0].clock_interval_ms");
        expectStr(d.fx[0].name, "WJ-BTN1", "t2 fx[0].name");
        expectInt(d.fx[0].ave5_button, 1, "t2 fx[0].ave5_button");
        expectTrue(d.fx[2].enabled == kExpF2Enabled, "t2 fx[2].enabled");
        expectInt(d.fx[2].trigger, kExpF2Trigger, "t2 fx[2].trigger");
        expectInt(d.fx[2].press_length_ms, kExpF2PressMs, "t2 fx[2].press_length_ms");
        expectInt(d.fx[2].press_count, kExpF2Count, "t2 fx[2].press_count");
        expectInt(d.fx[2].press_gap_ms, kExpF2GapMs, "t2 fx[2].press_gap_ms");
        expectTrue(d.fx[2].clock_enable == kExpF2ClockEn, "t2 fx[2].clock_enable");
        expectInt(d.fx[2].clock_interval_ms, kExpF2ClockIntervalMs,
                  "t2 fx[2].clock_interval_ms");
        expectStr(d.fx[2].name, kExpF2Name, "t2 fx[2].name");
        expectInt(d.fx[2].ave5_button, kExpF2Ave5Button, "t2 fx[2].ave5_button");
        expectTrue(d.fx[7].enabled == kExpF7Enabled, "t2 fx[7].enabled");
        expectInt(d.fx[7].trigger, kExpF7Trigger, "t2 fx[7].trigger");
        expectInt(d.fx[7].press_length_ms, kExpF7PressMs, "t2 fx[7].press_length_ms");
        expectInt(d.fx[7].press_count, kExpF7Count, "t2 fx[7].press_count");
        expectInt(d.fx[7].press_gap_ms, kExpF7GapMs, "t2 fx[7].press_gap_ms");
        expectTrue(d.fx[7].clock_enable == kExpF7ClockEn, "t2 fx[7].clock_enable");
        expectInt(d.fx[7].clock_interval_ms, kExpF7ClockIntervalMs,
                  "t2 fx[7].clock_interval_ms");
        expectStr(d.fx[7].name, kExpF7Name, "t2 fx[7].name");
        expectInt(d.fx[7].ave5_button, kExpF7Ave5Button, "t2 fx[7].ave5_button");
        // boot fields
        expectTrue(d.boot.enabled == kExpBootEnabled, "t2 boot.enabled");
        expectInt(d.boot.start_delay_ms, kExpBootStartDelayMs, "t2 boot.start_delay_ms");
        expectInt(d.boot.step_count, kExpBootStepCount, "t2 boot.step_count");
        expectInt(d.boot.steps[0].relay, kExpStep0Relay, "t2 boot.steps[0].relay");
        expectInt(d.boot.steps[0].presses, kExpStep0Presses, "t2 boot.steps[0].presses");
        expectInt(d.boot.steps[0].length_ms, kExpStep0LengthMs, "t2 boot.steps[0].length_ms");
        expectInt(d.boot.steps[0].gap_ms, kExpStep0GapMs, "t2 boot.steps[0].gap_ms");
        expectInt(d.boot.steps[0].wait_after_ms, kExpStep0WaitMs,
                  "t2 boot.steps[0].wait_after_ms");
        expectInt(d.boot.steps[1].relay, kExpStep1Relay, "t2 boot.steps[1].relay");
        expectInt(d.boot.steps[1].presses, kExpStep1Presses, "t2 boot.steps[1].presses");
        expectInt(d.boot.steps[1].length_ms, kExpStep1LengthMs, "t2 boot.steps[1].length_ms");
        expectInt(d.boot.steps[1].gap_ms, kExpStep1GapMs, "t2 boot.steps[1].gap_ms");
        expectInt(d.boot.steps[1].wait_after_ms, kExpStep1WaitMs,
                  "t2 boot.steps[1].wait_after_ms");
        // wifi + telemetry
        expectStr(d.wifi_ssid, kExpSsid, "t2 wifi_ssid");
        expectStr(d.wifi_password, kExpPassword, "t2 wifi_password");
    }

    // ---- (3) flipped payload byte -> Invalid, out unchanged ----
    {
        std::array<uint8_t, 1024> bad = env;
        bad[kEnvHeaderSize + 88] = static_cast<uint8_t>(bad[kEnvHeaderSize + 88] ^ 0xFFu);
        CalibrationConfig before = t2decoded;
        CalibrationConfig out = before;
        LoadResult lr = configurationDecode(bad.data(), envLen, out);
        expectTrue(lr.status == LoadStatus::Invalid, "t3 flipped payload byte -> Invalid");
        expectTrue(sameConfig(out, before), "t3 out-config unchanged after Invalid");
    }

    // ---- (4) truncated (len-1) and payload_len+2 appended ----
    {
        CalibrationConfig before = t2decoded;
        CalibrationConfig out = before;
        LoadResult lr = configurationDecode(env.data(), envLen - 1, out);
        expectTrue(lr.status == LoadStatus::Invalid, "t4 truncated (len-1) -> Invalid");
        expectTrue(sameConfig(out, before), "t4 truncated leaves out unchanged");
        std::array<uint8_t, 1026> ext{};
        std::memcpy(ext.data(), env.data(), envLen);
        ext[envLen] = 0xEE;
        ext[envLen + 1] = 0xDD;
        lr = configurationDecode(ext.data(), envLen + 2, out);
        expectTrue(lr.status == LoadStatus::Invalid,
                   "t4 payload_len+2 extra bytes appended -> Invalid");
        expectTrue(sameConfig(out, before), "t4 overlong leaves out unchanged");
    }

    // ---- (5) magic APP4 ----
    {
        std::array<uint8_t, 1024> bad = env;
        bad[0] = static_cast<uint8_t>('A');
        bad[1] = static_cast<uint8_t>('P');
        bad[2] = static_cast<uint8_t>('P');
        bad[3] = static_cast<uint8_t>('4');
        CalibrationConfig out = t2decoded;
        LoadResult lr = configurationDecode(bad.data(), envLen, out);
        expectTrue(lr.status == LoadStatus::Invalid, "t5 magic APP4 -> Invalid");
    }

    // ---- (6) envelope version 4; raw v4 has no envelope ----
    {
        std::array<uint8_t, 1024> bad = env;
        bad[4] = 4;  // envelope version byte
        CalibrationConfig out = t2decoded;
        LoadResult lr = configurationDecode(bad.data(), envLen, out);
        expectTrue(lr.status == LoadStatus::Invalid, "t6 envelope version 4 -> Invalid");
        // v4 blobs are raw images with no envelope: the first byte is the
        // D_min LSB (0x00 for 80.0f), never 'A', so the v5 decoder rejects.
        lr = configurationDecode(legacyV4Blob().data(), legacyV4Blob().size(), out);
        expectTrue(lr.status == LoadStatus::Invalid,
                   "t6 raw v4 image straight into v5 decoder -> Invalid");
    }

    // ---- (7) 580-byte v4 fixture -> Migrated, every field verified ----
    {
        CalibrationConfig mig{};
        LoadResult lr =
            loadConfigurationBlob(legacyV4Blob().data(), legacyV4Blob().size(), mig);
        expectTrue(lr.status == LoadStatus::Migrated, "t7 v4 fixture -> Migrated");
        expectTrue(lr.error == nullptr, "t7 Migrated carries no error");
        expectFloat(mig.D_min, kExpDMin, "t7 D_min");
        expectFloat(mig.D_max, kExpDMax, "t7 D_max");
        expectFloat(mig.hysteresis, kExpHysteresis, "t7 hysteresis");
        expectFloat(mig.gamma_exponent, kExpGamma, "t7 gamma_exponent");
        expectFloat(mig.slew_rate_limit, kExpSlew, "t7 slew_rate_limit");
        expectFloat(mig.breathing_depth_M, kExpBreathDepth, "t7 breathing_depth_M");
        expectInt(mig.pwm_min_clamp, kExpPwmMin, "t7 pwm_min_clamp");
        expectInt(mig.pwm_max_clamp, kExpPwmMax, "t7 pwm_max_clamp");
        expectTrue(mig.touch_inverted == kExpTouchInverted, "t7 touch_inverted");
        expectFloat(mig.variance_threshold_cm, kExpVariance, "t7 variance_threshold_cm");
        expectInt(mig.stationary_lock_time_ms, kExpStationaryMs, "t7 stationary_lock_time_ms");
        expectFloat(mig.agc_epsilon, kExpAgcEpsilon, "t7 agc_epsilon");
        expectInt(mig.agc_window_size, kExpAgcWindow, "t7 agc_window_size (u32 -> u16)");
        expectInt(mig.button_debounce_ms, kExpDebounceMs, "t7 button_debounce_ms");
        expectInt(mig.multiclick_window_ms, kExpMulticlickMs, "t7 multiclick_window_ms");
        expectInt(mig.long_press_ms, kExpLongPressMs, "t7 long_press_ms");
        expectFloat(mig.breath_threshold, kExpBreathThreshold, "t7 breath_threshold");
        expectInt(mig.auto_trigger_cooldown_ms, kExpCooldownMs,
                  "t7 auto_trigger_cooldown_ms");
        expectFloat(mig.pi_zone_far_cm, kExpZoneFarCm, "t7 pi_zone_far_cm");
        expectFloat(mig.pi_zone_near_cm, kExpZoneNearCm, "t7 pi_zone_near_cm");
        // counts 6/8 and all 28 migrated pin cells
        expectInt(mig.vactrol_count, kExpVactrolCount, "t7 vactrol_count");
        expectInt(mig.relay_count, kExpRelayCount, "t7 relay_count");
        expectInt(mig.layout.vactrol_count, kExpVactrolCount, "t7 layout.vactrol_count");
        expectInt(mig.layout.relay_count, kExpRelayCount, "t7 layout.relay_count");
        for (int i = 0; i < 14; ++i) {
            const int wantV = (i < 6) ? kExpVactrolPins[i] : -1;
            const int wantR = (i < 8) ? kExpRelayPins[i] : -1;
            std::snprintf(label, sizeof(label), "t7 layout.vactrol_pins[%d]", i);
            expectInt(mig.layout.vactrol_pins[i], wantV, label);
            std::snprintf(label, sizeof(label), "t7 layout.relay_pins[%d]", i);
            expectInt(mig.layout.relay_pins[i], wantR, label);
        }
        // vactrol rows
        expectTrue(mig.vactrol[0].auto_mode == kExpV0Auto, "t7 vactrol[0].auto_mode");
        expectInt(mig.vactrol[0].min_clamp, kExpV0Min, "t7 vactrol[0].min_clamp");
        expectInt(mig.vactrol[0].max_clamp, kExpV0Max, "t7 vactrol[0].max_clamp");
        expectFloat(mig.vactrol[0].slew_per_ms, kExpV0Slew, "t7 vactrol[0].slew_per_ms");
        expectInt(mig.vactrol[0].manual_value, kExpV0Manual, "t7 vactrol[0].manual_value");
        expectInt(mig.vactrol[0].ave5_pot, kExpV0Pot, "t7 vactrol[0].ave5_pot");
        expectTrue(!mig.vactrol[1].auto_mode, "t7 vactrol[1].auto_mode");
        expectInt(mig.vactrol[1].min_clamp, 0, "t7 vactrol[1].min_clamp");
        expectInt(mig.vactrol[1].max_clamp, 1023, "t7 vactrol[1].max_clamp");
        expectFloat(mig.vactrol[1].slew_per_ms, 2.0f, "t7 vactrol[1].slew_per_ms");
        expectInt(mig.vactrol[1].manual_value, 0, "t7 vactrol[1].manual_value");
        expectInt(mig.vactrol[1].ave5_pot, 0, "t7 vactrol[1].ave5_pot");
        expectTrue(mig.vactrol[2].auto_mode == kExpV2Auto, "t7 vactrol[2].auto_mode");
        expectInt(mig.vactrol[2].min_clamp, kExpV2Min, "t7 vactrol[2].min_clamp");
        expectInt(mig.vactrol[2].max_clamp, kExpV2Max, "t7 vactrol[2].max_clamp");
        expectFloat(mig.vactrol[2].slew_per_ms, kExpV2Slew, "t7 vactrol[2].slew_per_ms");
        expectInt(mig.vactrol[2].manual_value, kExpV2Manual, "t7 vactrol[2].manual_value");
        expectInt(mig.vactrol[2].ave5_pot, kExpV2Pot, "t7 vactrol[2].ave5_pot");
        for (int i : {3, 4, 5}) {
            const VactrolSettings& s = mig.vactrol[i];
            std::snprintf(label, sizeof(label), "t7 vactrol[%d] default row", i);
            expectTrue(!s.auto_mode && s.min_clamp == 0 && s.max_clamp == 1023 &&
                           s.slew_per_ms == 2.0f && s.manual_value == 0 && s.ave5_pot == 0,
                       label);
        }
        // fx rows
        expectTrue(fxZeroed(mig.fx[0]), "t7 fx[0] default zero row");
        expectTrue(fxZeroed(mig.fx[1]), "t7 fx[1] default zero row");
        expectTrue(mig.fx[2].enabled == kExpF2Enabled, "t7 fx[2].enabled");
        expectInt(mig.fx[2].trigger, kExpF2Trigger, "t7 fx[2].trigger");
        expectInt(mig.fx[2].press_length_ms, kExpF2PressMs, "t7 fx[2].press_length_ms");
        expectInt(mig.fx[2].press_count, kExpF2Count, "t7 fx[2].press_count");
        expectInt(mig.fx[2].press_gap_ms, kExpF2GapMs, "t7 fx[2].press_gap_ms");
        expectTrue(mig.fx[2].clock_enable == kExpF2ClockEn, "t7 fx[2].clock_enable");
        expectInt(mig.fx[2].clock_interval_ms, kExpF2ClockIntervalMs,
                  "t7 fx[2].clock_interval_ms (u16 zero-extended to u32)");
        expectStr(mig.fx[2].name, kExpF2Name, "t7 fx[2].name");
        expectInt(mig.fx[2].ave5_button, kExpF2Ave5Button, "t7 fx[2].ave5_button");
        for (int i : {3, 4, 5, 6}) {
            std::snprintf(label, sizeof(label), "t7 fx[%d] default zero row", i);
            expectTrue(fxZeroed(mig.fx[i]), label);
        }
        expectTrue(mig.fx[7].enabled == kExpF7Enabled, "t7 fx[7].enabled");
        expectInt(mig.fx[7].trigger, kExpF7Trigger, "t7 fx[7].trigger");
        expectInt(mig.fx[7].press_length_ms, kExpF7PressMs, "t7 fx[7].press_length_ms");
        expectInt(mig.fx[7].press_count, kExpF7Count, "t7 fx[7].press_count");
        expectInt(mig.fx[7].press_gap_ms, kExpF7GapMs, "t7 fx[7].press_gap_ms");
        expectTrue(mig.fx[7].clock_enable == kExpF7ClockEn, "t7 fx[7].clock_enable");
        expectInt(mig.fx[7].clock_interval_ms, kExpF7ClockIntervalMs,
                  "t7 fx[7].clock_interval_ms");
        expectStr(mig.fx[7].name, kExpF7Name, "t7 fx[7].name");
        expectInt(mig.fx[7].ave5_button, kExpF7Ave5Button, "t7 fx[7].ave5_button");
        // boot
        expectTrue(mig.boot.enabled == kExpBootEnabled, "t7 boot.enabled");
        expectInt(mig.boot.start_delay_ms, kExpBootStartDelayMs, "t7 boot.start_delay_ms");
        expectInt(mig.boot.step_count, kExpBootStepCount, "t7 boot.step_count");
        expectInt(mig.boot.steps[0].relay, kExpStep0Relay, "t7 boot.steps[0].relay");
        expectInt(mig.boot.steps[0].presses, kExpStep0Presses, "t7 boot.steps[0].presses");
        expectInt(mig.boot.steps[0].length_ms, kExpStep0LengthMs,
                  "t7 boot.steps[0].length_ms");
        expectInt(mig.boot.steps[0].gap_ms, kExpStep0GapMs, "t7 boot.steps[0].gap_ms");
        expectInt(mig.boot.steps[0].wait_after_ms, kExpStep0WaitMs,
                  "t7 boot.steps[0].wait_after_ms");
        expectInt(mig.boot.steps[1].relay, kExpStep1Relay, "t7 boot.steps[1].relay");
        expectInt(mig.boot.steps[1].presses, kExpStep1Presses, "t7 boot.steps[1].presses");
        expectInt(mig.boot.steps[1].length_ms, kExpStep1LengthMs,
                  "t7 boot.steps[1].length_ms");
        expectInt(mig.boot.steps[1].gap_ms, kExpStep1GapMs, "t7 boot.steps[1].gap_ms");
        expectInt(mig.boot.steps[1].wait_after_ms, kExpStep1WaitMs,
                  "t7 boot.steps[1].wait_after_ms");
        // wifi / telemetry / version
        expectStr(mig.wifi_ssid, kExpSsid, "t7 wifi_ssid");
        expectStr(mig.wifi_password, kExpPassword, "t7 wifi_password");
        expectInt(mig.telemetry_rate_hz, kExpTelemetryHz, "t7 telemetry_rate_hz");
        expectInt(mig.config_version, 5, "t7 config_version migrated to 5");
        // migrated result validates, structurally and topologically
        err = nullptr;
        expectTrue(configurationValidate(mig, &err),
                   "t7 migrated configurationValidate == true");
        AllocationResult ar = validateLayout(mig.layout);
        expectTrue(ar.ok, "t7 migrated validateLayout == true");
        t7migrated = mig;
        // the cached fixtureV4() parses to the same image as the dispatch
        CalibrationConfig viaFixture{};
        configurationMigrateFromV4(fixtureV4(), viaFixture);
        expectTrue(sameConfig(viaFixture, mig),
                   "t7 fixtureV4() migration matches blob dispatch");
    }

    // ---- (8) addOutput(Relay) on a migrated copy ----
    {
        CalibrationConfig grown = t7migrated;
        AllocationResult ar = addOutput(grown.layout, Bank::Relay);
        expectTrue(ar.ok, "t8 addOutput(Relay) succeeds");
        grown.relay_count = grown.layout.relay_count;
        err = nullptr;
        expectTrue(configurationValidate(grown, &err),
                   "t8 grown configurationValidate == true");
        expectTrue(validateLayout(grown.layout).ok, "t8 grown validateLayout == true");
    }

    // ---- (9) corrupt v4 bool byte at vactrol[2].auto_mode ----
    {
        std::array<unsigned char, kV4ImageSize> bad = legacyV4Blob();
        const size_t off = kV4OffVactrolBase + 2 * kV4VactrolStride;  // vactrol[2].auto_mode
        bad[off] = 2;
        CalibrationConfig out = t7migrated;
        LoadResult lr = loadConfigurationBlob(bad.data(), bad.size(), out);
        expectTrue(lr.status == LoadStatus::Invalid,
                   "t9 vactrol[2].auto_mode bool byte 2 -> Invalid");
        expectTrue(sameConfig(out, t7migrated), "t9 out unchanged after Invalid");
    }

    // ---- (10) v4 D_min=500 >= D_max=420 -> validation rejects after migration ----
    {
        std::array<unsigned char, kV4ImageSize> bad = legacyV4Blob();
        const float patchedDMin = 500.0f;
        uint32_t bits = 0;
        std::memcpy(&bits, &patchedDMin, 4);
        for (int i = 0; i < 4; ++i)
            bad[i] = static_cast<unsigned char>((bits >> (8 * i)) & 0xFFu);
        CalibrationConfig out = t7migrated;
        LoadResult lr = loadConfigurationBlob(bad.data(), bad.size(), out);
        expectTrue(lr.status == LoadStatus::Invalid,
                   "t10 v4 D_min=500 >= D_max=420 -> Invalid");
        expectTrue(sameConfig(out, t7migrated), "t10 out unchanged after Invalid");
    }

    // ---- (11) invalid config refused by encode, buffer untouched ----
    {
        CalibrationConfig bad = makeRoundtripSource();
        std::array<uint8_t, 1024> guard{};
        std::memset(guard.data(), 0x5A, guard.size());
        bad.layout.vactrol_pins[0] = 12;  // pin 12 is reserved -> layout invalid
        const size_t n = configurationEncode(bad, guard.data(), guard.size());
        expectInt(static_cast<long long>(n), 0, "t11 encode of invalid config returns 0");
        expectTrue(guard[0] == 0x5A, "t11 buffer first byte untouched on refusal");
    }

    // ---- (12) cap = needed-1 ----
    {
        CalibrationConfig src = makeRoundtripSource();
        const size_t need = configurationEncodedSize(src);
        std::array<uint8_t, 1024> buf{};
        const size_t n = configurationEncode(src, buf.data(), need - 1);
        expectInt(static_cast<long long>(n), 0, "t12 cap = needed-1 -> encode returns 0");
    }

    // ---- (13) declared payload_len larger than actual buffer ----
    {
        std::array<uint8_t, 1024> bad = env;
        const uint16_t plen = static_cast<uint16_t>(
            bad[kEnvPayloadLenOff] |
            (static_cast<uint16_t>(bad[kEnvPayloadLenOff + 1]) << 8));
        const uint16_t fake = static_cast<uint16_t>(plen + 4);
        bad[kEnvPayloadLenOff] = static_cast<uint8_t>(fake & 0xFFu);
        bad[kEnvPayloadLenOff + 1] = static_cast<uint8_t>(fake >> 8);
        CalibrationConfig out = t2decoded;
        LoadResult lr = configurationDecode(bad.data(), envLen, out);
        expectTrue(lr.status == LoadStatus::Invalid,
                   "t13 declared payload_len larger than actual -> Invalid");
        expectTrue(sameConfig(out, t2decoded), "t13 out unchanged after Invalid");
    }

    if (g_failures == 0) {
        std::printf("PASS: %d assertions\n", g_checks);
        return 0;
    }
    std::printf("FAILED: %d of %d assertions\n", g_failures, g_checks);
    return 1;
}
