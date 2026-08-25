/**
 * @file main.cpp
 * @brief The Apparatus — WJ-AVE5 edition
 *
 * Subsystem wiring per loop iteration (all non-blocking):
 *   RadarParser::update()      -> UART frame extraction / SIM generation
 *   DSPPipeline::process()     -> 10 Hz respiration extraction
 *   StateMachine::update()     -> 4-tier logic, normalized mix target
 *   VactrolManager::update()   -> 6-ch PWM with gamma/clamp/slew
 *   RelayManager::update()     -> press sequencers + auto triggers
 *   ButtonBank::update()       -> performer buttons
 *   WebConsole::broadcast()    -> 20 Hz telemetry
 *   PiLink                     -> serial commands to mpv daemon
 */

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "PinDefinitions.h"
#include "RadarParser.h"
#include "DSP.h"
#include "StateMachine.h"
#include "VactrolManager.h"
#include "RelayManager.h"
#include "WebConsole.h"

/* ============================================================================
 * GLOBALS
 * ============================================================================ */

CalibrationConfig g_config;
Preferences g_preferences;

HardwareSerial PiLink(1);   // UART1 remapped to GPIO 2 (TX-only)

RadarParser g_radar;
DSPPipeline g_dsp(g_config);
ApparatusStateMachine g_state_machine(g_dsp);
VactrolManager g_vactrols;
RelayManager g_relays;
ButtonBank g_buttons;
BootSequencer g_boot;
WebConsole g_web(80);

volatile bool g_touch_triggered = false;
volatile uint32_t g_touch_last_isr_us = 0;
portMUX_TYPE g_touch_mux = portMUX_INITIALIZER_UNLOCKED;

static constexpr uint32_t MAIN_LOOP_TARGET_MS = 10;

/* ============================================================================
 * TOUCH ISR (GPIO 34, external pull-down hardware assumed)
 * ============================================================================ */

void IRAM_ATTR touchISR() {
    uint32_t now = micros();
    if (now - g_touch_last_isr_us > 5000) {
        g_touch_last_isr_us = now;
        bool state = (digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL);
        if (g_config.touch_inverted) state = !state;
        portENTER_CRITICAL_ISR(&g_touch_mux);
        g_touch_triggered = state;
        portEXIT_CRITICAL_ISR(&g_touch_mux);
    }
}

/* ============================================================================
 * NVS PERSISTENCE
 * ============================================================================ */

void loadConfiguration() {
    g_preferences.begin(NVS_NAMESPACE, true);
    if (g_preferences.isKey(NVS_KEY_CONFIG)) {
        size_t len = g_preferences.getBytesLength(NVS_KEY_CONFIG);
        CalibrationConfig loaded;
        if (len == sizeof(CalibrationConfig) &&
            g_preferences.getBytes(NVS_KEY_CONFIG, &loaded, len) == len &&
            loaded.config_version == CONFIG_VERSION) {
            g_config = loaded;
            log_i("Config loaded from NVS (v%u)", loaded.config_version);
        } else {
            log_w("NVS config stale (size %u, v%u) - defaults", len,
                  len >= sizeof(uint8_t) ? 0 : 0);
            // Keep defaults on mismatch; struct layout changed -> fresh start
        }
    } else {
        log_i("No NVS config - defaults");
    }
    g_preferences.end();
}

void saveConfiguration() {
    g_preferences.begin(NVS_NAMESPACE, false);
    g_config.config_version = CONFIG_VERSION;
    g_preferences.putBytes(NVS_KEY_CONFIG, &g_config, sizeof(CalibrationConfig));
    g_preferences.end();
}

static volatile bool g_factory_reset_requested = false;
void requestFactoryReset() { g_factory_reset_requested = true; }

// WebConsole handlers
static void relayFireFromGui(uint8_t idx) { g_relays.fireSequence(idx); }
static void relayStopFromGui(uint8_t idx) { g_relays.stopSequence(idx); }

/* ============================================================================
 * SETUP
 * ============================================================================ */

void setup() {
    Serial.begin(115200);
    delay(100);

    log_i("========================================");
    log_i("  THE APPARATUS - WJ-AVE5 Edition");
    log_i("  ESP32-WROOM-32 | LD2410 | Vactrols x6");
    log_i("  Relays x8 | Buttons x%d | PiLink", BUTTON_MAX);
    log_i("========================================");

    loadConfiguration();

    // --- GPIO: relays first (boot-safe HIGH = released) ---
    g_relays.begin();

    // Touch input
    pinMode(TOUCH_PIN, INPUT);   // GPIO34: input-only, external pull-down
    attachInterrupt(digitalPinToInterrupt(TOUCH_PIN), touchISR, CHANGE);

    // Performer buttons
    g_buttons.begin();

    // Pi link serial
    PiLink.begin(PI_LINK_BAUD, SERIAL_8N1, -1, PI_LINK_TX_PIN);

    // Vactrol PWM engine
    g_vactrols.begin();

    // Watchdog
    esp_task_wdt_init(8, true);
    esp_task_wdt_add(NULL);

#ifdef APPARATUS_SIM_MODE
    log_i("*** SIM MODE ***");
#else
    if (!g_radar.begin()) {
        log_e("Radar init failed - continuing radarless");
    }
#endif

    // WiFi AP + console
    WiFi.mode(WIFI_AP);
    WiFi.softAP(g_config.wifi_ssid, g_config.wifi_password);
    log_i("AP '%s' @ %s", g_config.wifi_ssid, WiFi.softAPIP().toString().c_str());

    g_web.setRelayFireHandler(relayFireFromGui);
    g_web.setRelayStopHandler(relayStopFromGui);
    g_web.begin();

    // Power-on ritual for the WJ-AVE5 (mixer power click etc.)
    g_boot.start();

    log_i("Setup complete.");
}

/* ============================================================================
 * MAIN LOOP
 * ============================================================================ */

void loop() {
    uint32_t loop_start = millis();
    esp_task_wdt_reset();

    // 1. Radar
    g_radar.update();

    // 2. Touch state
    bool touch_active;
    portENTER_CRITICAL(&g_touch_mux);
    touch_active = g_touch_triggered;
    portEXIT_CRITICAL(&g_touch_mux);

    // 3. DSP pipeline at 10 Hz
    RadarFrame current_frame = g_radar.getLatestFrame();
    bool dsp_ready = false;
    float dsp_norm = 0.0f, dsp_biquad = 0.0f;
    if (g_radar.hasNewFrame()) {
        g_radar.clearNewFrameFlag();
        dsp_ready = g_dsp.process(current_frame, dsp_norm, dsp_biquad);
    }

    // 4. State machine
    ApparatusState_t prev_state = g_state_machine.getState();
    g_state_machine.update(touch_active, current_frame, dsp_ready, dsp_norm, dsp_biquad);
    ApparatusState_t now_state = g_state_machine.getState();
    bool state_changed = (prev_state != now_state);

    // 5. Vactrols (mix target normalized 0-1 from state machine)
    float mix_target = g_state_machine.getPWMOutputFloat();
    g_vactrols.update(mix_target, now_state == STATE_CONTACT);

    // 6. Relays + performer buttons + boot ritual
    g_relays.update(state_changed, now_state, dsp_ready ? dsp_norm : 0.0f);
    g_buttons.update(g_relays);
    g_boot.update(g_relays);

    // 7. Factory reset check
    if (g_factory_reset_requested) {
        g_factory_reset_requested = false;
        g_config = CalibrationConfig();
        saveConfiguration();
        log_w("Factory reset - restarting");
        delay(200);
        ESP.restart();
    }

    // 8. Telemetry 20 Hz
    static uint32_t last_tele = 0;
    if (millis() - last_tele >= 50) {
        last_tele = millis();
        TelemetryPacket p;
        p.timestamp_ms = millis();
        p.state = now_state;
        const RadarFrame& f = g_radar.getLatestFrame();
        p.distance_raw = f.detection_distance_cm;
        p.distance_filtered = g_dsp.getDistanceFiltered();
        for (int i = 0; i < RADAR_GATE_COUNT; i++)
            p.stationary_energy[i] = f.stationary_gate_energy[i];
        p.biquad_raw = g_dsp.getBiquadRaw();
        p.agc_normalized = g_dsp.getAGCNormalized();
        p.peak_gate = g_dsp.getPeakGate();
        p.mix_pwm = g_vactrols.getValue(VACT_MIX);
        p.pi_trigger = g_state_machine.getPiTrigger();
        p.gamma_shaped = g_state_machine.getGammaShaped();
        p.base_pwm_f = g_state_machine.getBasePWM();
        for (int i = 0; i < RELAY_COUNT; i++) {
            p.relay_seq[i] = g_relays.getSeqActive(i);
            p.relay_pressed[i] = g_relays.isPressed(i);
        }
        for (int i = 0; i < VACTROL_COUNT; i++) {
            p.vactrol_val[i] = g_vactrols.getValue(i);
            p.vactrol_auto[i] = g_config.vactrol[i].auto_mode;
        }
        g_web.broadcastTelemetry(p);
    }

    // 9. Status print every 10 s
    static uint32_t last_status = 0;
    if (millis() - last_status > 10000) {
        last_status = millis();
        log_i("St=%s mix=%.2f dist=%.0f agc=%.2f frames=%lu err=%lu ws=%u",
              g_state_machine.getStateName(), mix_target,
              g_dsp.getDistanceFiltered(), g_dsp.getAGCNormalized(),
              (unsigned long)g_radar.getFramesReceived(),
              (unsigned long)g_radar.getParseErrors(),
              (unsigned)g_web.getClientCount());
    }

    // 10. Heartbeat LED
    static uint32_t last_led = 0;
    static bool led = false;
    if (millis() - last_led > 1000) {
        last_led = millis();
        led = !led;
        digitalWrite(STATUS_LED_PIN, led);
    }

    // Loop pacing
    uint32_t spent = millis() - loop_start;
    if (spent < MAIN_LOOP_TARGET_MS) {
        delay(MAIN_LOOP_TARGET_MS - spent);
    }
}