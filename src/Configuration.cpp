#include "Configuration.h"

#include <cstring>

namespace apparatus {
namespace {

constexpr uint8_t kMagic[4] = {'A', 'P', 'P', '5'};
constexpr uint8_t kVersion = 5;
constexpr size_t kHeaderSize = 12;  // magic(4) ver(1) flags(1) payload_len(2) crc32(4)
constexpr size_t kMaxBootSteps = 12;
constexpr size_t kVactrolRec = 12;  // u8 auto,u16 min,u16 max,f32 slew,u16 manual,u8 pot
constexpr size_t kFxRec = 25;  // u8 en,u8 trig,u16 len,u8 cnt,u16 gap,u8 cen,u32 cint,name[12],u8 pot
constexpr size_t kStepRec = 8;  // u8 relay,u8 presses,u16 len,u16 gap,u16 wait
// Payload before boot steps: scalars(71) + wifi(96) + telemetry(2) +
// version/counts(3) + pins(28) + vactrol rows(168) + fx rows(350) + boot base(4).
constexpr size_t kFixedPayload = 722;

void putU16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v & 0xFFu); p[1] = uint8_t(v >> 8); }
void putU32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t((v >> (8 * i)) & 0xFFu);
}
void putF32(uint8_t* p, float v) { uint32_t b; memcpy(&b, &v, 4); putU32(p, b); }
uint16_t getU16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
uint32_t getU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
float getF32(const uint8_t* p) { uint32_t b = getU32(p); float v; memcpy(&v, &b, 4); return v; }

// IEEE CRC-32: reflected poly 0xEDB88320, init 0xFFFFFFFF, final XOR.
uint32_t crc32IEEE(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

// NaN-safe range check (NaN fails both comparisons).
bool inRange(float v, float lo, float hi) { return v >= lo && v <= hi; }

// Strict bool byte: only 0/1 accepted; consumes the byte.
bool boolByte(const uint8_t* p, size_t* o, bool* v) {
    if (p[*o] > 1) return false;
    *v = (p[*o] != 0);
    ++*o;
    return true;
}

bool zeroTerminated(const char* s, size_t cap) {
    for (size_t i = 0; i < cap; ++i)
        if (s[i] == '\0') return true;
    return false;
}

}  // namespace

bool configurationValidate(const CalibrationConfig& c, const char** error) {
    if (error == nullptr) return false;
    auto fail = [error](const char* m) { *error = m; return false; };
    AllocationResult ar = validateLayout(c.layout);
    if (!ar.ok) { *error = ar.error; return false; }
    if (c.layout.vactrol_count != c.vactrol_count || c.layout.relay_count != c.relay_count)
        return fail("layout counts differ from configuration counts");
    if (!inRange(c.D_min, 10.0f, 500.0f)) return fail("D_min out of range");
    if (!inRange(c.D_max, 50.0f, 675.0f)) return fail("D_max out of range");
    if (!(c.D_min < c.D_max)) return fail("D_min must be < D_max");
    if (!inRange(c.hysteresis, 0.0f, 100.0f)) return fail("hysteresis out of range");
    if (!inRange(c.gamma_exponent, 0.1f, 4.0f)) return fail("gamma_exponent out of range");
    if (!inRange(c.slew_rate_limit, 0.1f, 20.0f)) return fail("slew_rate_limit out of range");
    if (!inRange(c.breathing_depth_M, 0.0f, 1.0f)) return fail("breathing_depth_M out of range");
    if (c.pwm_min_clamp > c.pwm_max_clamp) return fail("pwm clamps out of order");
    if (!inRange(c.variance_threshold_cm, 1.0f, 30.0f)) return fail("variance_threshold_cm out of range");
    if (c.stationary_lock_time_ms > 60000u) return fail("stationary_lock_time_ms out of range");
    if (!inRange(c.agc_epsilon, 1e-9f, 1e-3f)) return fail("agc_epsilon out of range");
    if (c.agc_window_size < 10 || c.agc_window_size > 200) return fail("agc_window_size out of range");
    if (c.button_debounce_ms < 5u || c.button_debounce_ms > 200u) return fail("button_debounce_ms out of range");
    if (c.multiclick_window_ms < 100u || c.multiclick_window_ms > 1000u) return fail("multiclick_window_ms out of range");
    if (c.long_press_ms < 200u || c.long_press_ms > 2000u) return fail("long_press_ms out of range");
    if (!inRange(c.breath_threshold, 0.05f, 1.0f)) return fail("breath_threshold out of range");
    if (c.auto_trigger_cooldown_ms < 100u || c.auto_trigger_cooldown_ms > 60000u) return fail("auto_trigger_cooldown_ms out of range");
    if (!inRange(c.pi_zone_far_cm, 40.0f, 650.0f)) return fail("pi_zone_far_cm out of range");
    if (!inRange(c.pi_zone_near_cm, 40.0f, 650.0f)) return fail("pi_zone_near_cm out of range");
    if (c.telemetry_rate_hz < 1u || c.telemetry_rate_hz > 100u) return fail("telemetry_rate_hz out of range");
    for (int i = 0; i < c.vactrol_count && i < BANK_CAPACITY; ++i) {
        const VactrolSettings& s = c.vactrol[size_t(i)];
        if (s.min_clamp > 1023u || s.max_clamp > 1023u || s.min_clamp > s.max_clamp)
            return fail("vactrol clamps out of range");
        if (s.manual_value > 1023u) return fail("vactrol manual_value out of range");
        if (!inRange(s.slew_per_ms, 0.1f, 50.0f)) return fail("vactrol slew_per_ms out of range");
        if (int(s.ave5_pot) >= AVE5_POT_COUNT) return fail("vactrol ave5_pot out of range");
    }
    for (int i = 0; i < c.relay_count && i < BANK_CAPACITY; ++i) {
        const FxRelaySettings& s = c.fx[size_t(i)];
        if (!s.enabled) continue;  // disabled rows carry no constraints
        if (s.trigger > 7u) return fail("fx trigger out of range");
        if (s.press_length_ms < 30u || s.press_length_ms > 3000u) return fail("fx press_length_ms out of range");
        if (s.press_count < 1u || s.press_count > 5u) return fail("fx press_count out of range");
        if (s.press_gap_ms < 20u || s.press_gap_ms > 2000u) return fail("fx press_gap_ms out of range");
        if (s.clock_interval_ms < 500u || s.clock_interval_ms > 600000u) return fail("fx clock_interval_ms out of range");
        if (int(s.ave5_button) >= AVE5_BUTTON_COUNT) return fail("fx ave5_button out of range");
        if (!zeroTerminated(s.name, 12)) return fail("fx name not NUL-terminated within 12");
    }
    if (c.boot.step_count > kMaxBootSteps) return fail("boot step_count out of range");
    for (size_t i = 0; i < size_t(c.boot.step_count); ++i) {
        const BootStep& s = c.boot.steps[i];
        if (int(s.relay) >= c.relay_count) return fail("boot step relay index out of range");
        if (s.presses < 1u || s.presses > 5u) return fail("boot step presses out of range");
        if (s.length_ms < 30u || s.length_ms > 5000u) return fail("boot step length_ms out of range");
        if (s.gap_ms < 20u || s.gap_ms > 5000u) return fail("boot step gap_ms out of range");
        if (s.wait_after_ms < 50u || s.wait_after_ms > 60000u) return fail("boot step wait_after_ms out of range");
    }
    if (!zeroTerminated(c.wifi_ssid, 32)) return fail("wifi_ssid not NUL-terminated");
    if (!zeroTerminated(c.wifi_password, 64)) return fail("wifi_password not NUL-terminated");
    *error = nullptr;
    return true;
}

size_t configurationEncodedSize(const CalibrationConfig& c) {
    return kHeaderSize + kFixedPayload + size_t(c.boot.step_count) * kStepRec;
}

size_t configurationEncode(const CalibrationConfig& c, uint8_t* out, size_t cap) {
    const char* verr = nullptr;
    if (out == nullptr || !configurationValidate(c, &verr)) return 0;
    const size_t total = configurationEncodedSize(c);
    if (cap < total) return 0;
    const uint16_t plen = uint16_t(total - kHeaderSize);
    for (int i = 0; i < 4; ++i) out[size_t(i)] = kMagic[size_t(i)];
    out[4] = kVersion;
    out[5] = 0;  // flags
    putU16(out + 6, plen);
    size_t o = kHeaderSize;
    putF32(out + o, c.D_min);             o += 4;
    putF32(out + o, c.D_max);             o += 4;
    putF32(out + o, c.hysteresis);        o += 4;
    putF32(out + o, c.gamma_exponent);    o += 4;
    putF32(out + o, c.slew_rate_limit);   o += 4;
    putF32(out + o, c.breathing_depth_M); o += 4;
    out[o++] = c.pwm_min_clamp;
    out[o++] = c.pwm_max_clamp;
    out[o++] = c.touch_inverted ? 1 : 0;
    putF32(out + o, c.variance_threshold_cm);     o += 4;
    putU32(out + o, c.stationary_lock_time_ms);   o += 4;
    putF32(out + o, c.agc_epsilon);               o += 4;
    putU32(out + o, uint32_t(c.agc_window_size)); o += 4;
    putU32(out + o, c.button_debounce_ms);        o += 4;
    putU32(out + o, c.multiclick_window_ms);      o += 4;
    putU32(out + o, c.long_press_ms);             o += 4;
    putF32(out + o, c.breath_threshold);          o += 4;
    putU32(out + o, c.auto_trigger_cooldown_ms);  o += 4;
    putF32(out + o, c.pi_zone_far_cm);            o += 4;
    putF32(out + o, c.pi_zone_near_cm);           o += 4;
    for (size_t i = 0; i < 32; ++i) out[o++] = uint8_t(c.wifi_ssid[i]);
    for (size_t i = 0; i < 64; ++i) out[o++] = uint8_t(c.wifi_password[i]);
    putU16(out + o, c.telemetry_rate_hz); o += 2;
    out[o++] = c.config_version;
    out[o++] = uint8_t(c.vactrol_count);
    out[o++] = uint8_t(c.relay_count);
    for (size_t i = 0; i < 14; ++i) out[o++] = uint8_t(int8_t(c.layout.vactrol_pins[i]));
    for (size_t i = 0; i < 14; ++i) out[o++] = uint8_t(int8_t(c.layout.relay_pins[i]));
    for (size_t i = 0; i < 14; ++i) {
        if (i < size_t(c.vactrol_count)) {
            const VactrolSettings& s = c.vactrol[i];
            out[o++] = s.auto_mode ? 1 : 0;
            putU16(out + o, s.min_clamp);    o += 2;
            putU16(out + o, s.max_clamp);    o += 2;
            putF32(out + o, s.slew_per_ms);  o += 4;
            putU16(out + o, s.manual_value); o += 2;
            out[o++] = s.ave5_pot;
        } else {
            for (size_t k = 0; k < kVactrolRec; ++k) out[o++] = 0;
        }
    }
    for (size_t i = 0; i < 14; ++i) {
        if (i < size_t(c.relay_count)) {
            const FxRelaySettings& s = c.fx[i];
            out[o++] = s.enabled ? 1 : 0;
            out[o++] = s.trigger;
            putU16(out + o, s.press_length_ms); o += 2;
            out[o++] = s.press_count;
            putU16(out + o, s.press_gap_ms);    o += 2;
            out[o++] = s.clock_enable ? 1 : 0;
            putU32(out + o, s.clock_interval_ms); o += 4;
            for (size_t k = 0; k < 12; ++k) out[o++] = uint8_t(s.name[k]);
            out[o++] = s.ave5_button;
        } else {
            for (size_t k = 0; k < kFxRec; ++k) out[o++] = 0;
        }
    }
    out[o++] = c.boot.enabled ? 1 : 0;
    putU16(out + o, c.boot.start_delay_ms); o += 2;
    out[o++] = c.boot.step_count;
    for (size_t i = 0; i < size_t(c.boot.step_count); ++i) {
        const BootStep& s = c.boot.steps[i];
        out[o++] = s.relay;
        out[o++] = s.presses;
        putU16(out + o, s.length_ms);     o += 2;
        putU16(out + o, s.gap_ms);        o += 2;
        putU16(out + o, s.wait_after_ms); o += 2;
    }
    if (o != total) return 0;  // internal consistency guard
    putU32(out + 8, crc32IEEE(out + kHeaderSize, size_t(plen)));
    return total;
}

LoadResult configurationDecode(const uint8_t* data, size_t len, CalibrationConfig& out) {
    if (data == nullptr || len < kHeaderSize) return {LoadStatus::Invalid, "blob too short"};
    if (data[0] != kMagic[0] || data[1] != kMagic[1] || data[2] != kMagic[2] || data[3] != kMagic[3])
        return {LoadStatus::Invalid, "bad magic"};
    if (data[4] != kVersion) return {LoadStatus::Invalid, "unsupported envelope version"};
    if (data[5] != 0) return {LoadStatus::Invalid, "unsupported envelope flags"};
    const uint16_t plen = getU16(data + 6);
    if (len != kHeaderSize + size_t(plen)) return {LoadStatus::Invalid, "length mismatch"};
    if (plen < kFixedPayload || plen > kFixedPayload + kMaxBootSteps * kStepRec ||
        (size_t(plen) - kFixedPayload) % kStepRec != 0)
        return {LoadStatus::Invalid, "payload length invalid"};
    if (crc32IEEE(data + kHeaderSize, size_t(plen)) != getU32(data + 8))
        return {LoadStatus::Invalid, "crc mismatch"};

    CalibrationConfig t{};  // 'out' stays untouched until the blob fully validates
    const uint8_t* p = data + kHeaderSize;
    size_t o = 0;
    t.D_min = getF32(p + o);             o += 4;
    t.D_max = getF32(p + o);             o += 4;
    t.hysteresis = getF32(p + o);        o += 4;
    t.gamma_exponent = getF32(p + o);    o += 4;
    t.slew_rate_limit = getF32(p + o);   o += 4;
    t.breathing_depth_M = getF32(p + o); o += 4;
    t.pwm_min_clamp = p[o++];
    t.pwm_max_clamp = p[o++];
    if (!boolByte(p, &o, &t.touch_inverted)) return {LoadStatus::Invalid, "invalid bool byte"};
    t.variance_threshold_cm = getF32(p + o);     o += 4;
    t.stationary_lock_time_ms = getU32(p + o);   o += 4;
    t.agc_epsilon = getF32(p + o);               o += 4;
    const uint32_t agcw = getU32(p + o);
    if (agcw > 200u) return {LoadStatus::Invalid, "agc_window_size out of range"};
    t.agc_window_size = uint16_t(agcw); o += 4;
    t.button_debounce_ms = getU32(p + o);        o += 4;
    t.multiclick_window_ms = getU32(p + o);      o += 4;
    t.long_press_ms = getU32(p + o);             o += 4;
    t.breath_threshold = getF32(p + o);          o += 4;
    t.auto_trigger_cooldown_ms = getU32(p + o);  o += 4;
    t.pi_zone_far_cm = getF32(p + o);            o += 4;
    t.pi_zone_near_cm = getF32(p + o);           o += 4;
    for (size_t i = 0; i < 32; ++i) t.wifi_ssid[i] = char(p[o++]);
    for (size_t i = 0; i < 64; ++i) t.wifi_password[i] = char(p[o++]);
    t.telemetry_rate_hz = getU16(p + o); o += 2;
    t.config_version = p[o++];
    t.vactrol_count = p[o++];
    t.relay_count = p[o++];
    for (size_t i = 0; i < 14; ++i) { int b = int(p[o++]); t.layout.vactrol_pins[i] = b >= 128 ? b - 256 : b; }
    for (size_t i = 0; i < 14; ++i) { int b = int(p[o++]); t.layout.relay_pins[i] = b >= 128 ? b - 256 : b; }
    t.layout.vactrol_count = t.vactrol_count;
    t.layout.relay_count = t.relay_count;
    for (size_t i = 0; i < 14; ++i) {
        VactrolSettings s{};
        if (!boolByte(p, &o, &s.auto_mode)) return {LoadStatus::Invalid, "invalid bool byte"};
        s.min_clamp = getU16(p + o);    o += 2;
        s.max_clamp = getU16(p + o);    o += 2;
        s.slew_per_ms = getF32(p + o);  o += 4;
        s.manual_value = getU16(p + o); o += 2;
        s.ave5_pot = p[o++];
        t.vactrol[i] = s;
    }
    for (size_t i = 0; i < 14; ++i) {
        FxRelaySettings s{};
        if (!boolByte(p, &o, &s.enabled)) return {LoadStatus::Invalid, "invalid bool byte"};
        s.trigger = p[o++];
        s.press_length_ms = getU16(p + o); o += 2;
        s.press_count = p[o++];
        s.press_gap_ms = getU16(p + o);    o += 2;
        if (!boolByte(p, &o, &s.clock_enable)) return {LoadStatus::Invalid, "invalid bool byte"};
        s.clock_interval_ms = getU32(p + o); o += 4;
        for (size_t k = 0; k < 12; ++k) s.name[k] = char(p[o++]);
        s.ave5_button = p[o++];
        t.fx[i] = s;
    }
    if (!boolByte(p, &o, &t.boot.enabled)) return {LoadStatus::Invalid, "invalid bool byte"};
    t.boot.start_delay_ms = getU16(p + o); o += 2;
    const uint8_t steps = p[o++];
    if (steps > kMaxBootSteps) return {LoadStatus::Invalid, "boot step_count out of range"};
    if (o + size_t(steps) * kStepRec != size_t(plen)) return {LoadStatus::Invalid, "boot step payload mismatch"};
    t.boot.step_count = steps;
    for (size_t i = 0; i < size_t(steps); ++i) {
        BootStep s{};
        s.relay = p[o++];
        s.presses = p[o++];
        s.length_ms = getU16(p + o);     o += 2;
        s.gap_ms = getU16(p + o);        o += 2;
        s.wait_after_ms = getU16(p + o); o += 2;
        t.boot.steps[i] = s;
    }
    const char* verr = nullptr;
    if (!configurationValidate(t, &verr)) return {LoadStatus::Invalid, verr};
    out = t;
    return {LoadStatus::Ok, nullptr};
}

void configurationMigrateFromV4(const CalibrationConfigV4& o, CalibrationConfig& n) {
    n = CalibrationConfig{};  // defaults first, then overwrite from legacy
    n.D_min = o.D_min;                     n.D_max = o.D_max;
    n.hysteresis = o.hysteresis;           n.gamma_exponent = o.gamma_exponent;
    n.slew_rate_limit = o.slew_rate_limit; n.breathing_depth_M = o.breathing_depth_M;
    n.pwm_min_clamp = o.pwm_min_clamp;     n.pwm_max_clamp = o.pwm_max_clamp;
    n.touch_inverted = o.touch_inverted;   n.variance_threshold_cm = o.variance_threshold_cm;
    n.stationary_lock_time_ms = o.stationary_lock_time_ms;
    n.agc_epsilon = o.agc_epsilon;
    n.agc_window_size =
        (o.agc_window_size >= 10 && o.agc_window_size <= 200) ? uint16_t(o.agc_window_size) : 40;
    n.button_debounce_ms = o.button_debounce_ms; n.multiclick_window_ms = o.multiclick_window_ms;
    n.long_press_ms = o.long_press_ms;           n.breath_threshold = o.breath_threshold;
    n.auto_trigger_cooldown_ms = o.auto_trigger_cooldown_ms;
    n.pi_zone_far_cm = o.pi_zone_far_cm;         n.pi_zone_near_cm = o.pi_zone_near_cm;
    for (size_t i = 0; i < 6; ++i) n.vactrol[i] = o.vactrol[i];
    for (size_t i = 0; i < 8; ++i) {
        const FxRelaySettingsV4& s = o.fx[i];
        FxRelaySettings d{};
        d.enabled = s.enabled;                 d.trigger = s.trigger;
        d.press_length_ms = s.press_length_ms; d.press_count = s.press_count;
        d.press_gap_ms = s.press_gap_ms;       d.clock_enable = s.clock_enable;
        d.clock_interval_ms = uint32_t(s.clock_interval_ms);  // zero-extend u16 -> u32
        for (size_t k = 0; k < 12; ++k) d.name[k] = s.name[k];
        d.ave5_button = s.ave5_button;
        n.fx[i] = d;
    }
    n.boot = o.boot;
    for (size_t i = 0; i < 32; ++i) n.wifi_ssid[i] = o.wifi_ssid[i];
    for (size_t i = 0; i < 64; ++i) n.wifi_password[i] = o.wifi_password[i];
    n.telemetry_rate_hz = o.telemetry_rate_hz;
    n.vactrol_count = 6;
    n.relay_count = 8;
    n.layout.vactrol_count = 6;
    n.layout.relay_count = 8;
    for (size_t i = 0; i < 14; ++i) {
        n.layout.vactrol_pins[i] = (i < 6) ? LEGACY_VACTROL_PINS[i] : -1;
        n.layout.relay_pins[i] = (i < 8) ? LEGACY_RELAY_PINS[i] : -1;
    }
    n.config_version = 5;
}

}  // namespace apparatus
