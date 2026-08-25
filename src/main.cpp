/**
 * @file main.cpp
 * @brief Main Entry Point for The Apparatus Firmware
 * 
 * Initializes all subsystems, runs the main control loop at ~100 Hz,
 * handles touch interrupts, manages NVS configuration persistence,
 * and coordinates the radar DSP pipeline, state machine, and web server.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "Config.h"
#include "Radar.h"
#include "DSP.h"
#include "StateMachine.h"
#include "WebServer.h"
#include "FXRelay.h"

/* ============================================================================
 * GLOBAL INSTANCES
 * ============================================================================ */

// Configuration (loaded from NVS)
CalibrationConfig g_config;
Preferences g_preferences;

// Hardware drivers
RadarDriver g_radar;
DSPPipeline g_dsp(g_config);
ApparatusStateMachine g_state_machine(g_config, g_dsp);
ApparatusWebServer g_web_server(80);
FXRelayController g_fx_relays;

// Touch interrupt handling
volatile bool g_touch_triggered = false;
volatile uint32_t g_touch_interrupt_time = 0;
portMUX_TYPE g_touch_mux = portMUX_INITIALIZER_UNLOCKED;

// Main loop timing
static constexpr uint32_t MAIN_LOOP_TARGET_MS = 10;  // 100 Hz main loop
uint32_t g_loop_start_time = 0;
uint32_t g_last_status_print = 0;

/* ============================================================================
 * TOUCH INTERRUPT HANDLER
 * ============================================================================ */

void IRAM_ATTR touchInterruptHandler() {
    // Debounce in ISR - minimum 5ms between interrupts
    uint32_t now = micros();
    if (now - g_touch_interrupt_time > 5000) {
        g_touch_interrupt_time = now;
        // Read current pin state
        bool touch_state = digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL;
        if (g_config.touch_inverted) touch_state = !touch_state;
        
        // Atomic update
        portENTER_CRITICAL_ISR(&g_touch_mux);
        g_touch_triggered = touch_state;
        portEXIT_CRITICAL_ISR(&g_touch_mux);
    }
}

/* ============================================================================
 * NVS CONFIGURATION MANAGEMENT
 * ============================================================================ */

void loadConfiguration() {
    g_preferences.begin(NVS_NAMESPACE, true);  // Read-only first
    
    if (g_preferences.isKey(NVS_KEY_CONFIG)) {
        size_t len = g_preferences.getBytesLength(NVS_KEY_CONFIG);
        if (len == sizeof(CalibrationConfig)) {
            g_preferences.getBytes(NVS_KEY_CONFIG, &g_config, len);
            // Version check for future migrations
            if (g_config.config_version != 1) {
                log_w("Config version mismatch, using defaults");
                g_config = CalibrationConfig();  // Reset to defaults
            } else {
                log_i("Configuration loaded from NVS");
            }
        } else {
            log_w("Config size mismatch, using defaults");
            g_config = CalibrationConfig();
        }
    } else {
        log_i("No config in NVS, using defaults");
        g_config = CalibrationConfig();
    }
    
    g_preferences.end();
}

void saveConfiguration() {
    g_preferences.begin(NVS_NAMESPACE, false);  // Read-write
    g_config.config_version = 1;
    g_preferences.putBytes(NVS_KEY_CONFIG, &g_config, sizeof(CalibrationConfig));
    g_preferences.end();
    log_i("Configuration saved to NVS");
}

void resetConfiguration() {
    g_config = CalibrationConfig();
    saveConfiguration();
    log_i("Configuration reset to defaults");
}

/* ============================================================================
 * HARDWARE INITIALIZATION
 * ============================================================================ */

void initHardware() {
    // Status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // Pi Trigger Pin
    pinMode(PI_TRIGGER_PIN, OUTPUT);
    digitalWrite(PI_TRIGGER_PIN, !PI_TRIGGER_ACTIVE_LEVEL);  // Inactive state
    
    // FX Relay Pins (stubbed - initialize to default states)
    uint8_t fx_pins[FX_RELAY_COUNT] = FX_RELAY_PINS;
    for (int i = 0; i < FX_RELAY_COUNT; i++) {
        pinMode(fx_pins[i], OUTPUT);
        digitalWrite(fx_pins[i], FX_RELAY_CONFIGS[i].default_state ? FX_RELAY_ACTIVE_LEVEL : !FX_RELAY_ACTIVE_LEVEL);
    }
    
    // Initialize FX Relay Controller
    g_fx_relays.begin();
    
    // Touch Input with Interrupt
    pinMode(TOUCH_PIN, INPUT_PULLDOWN);  // Adjust based on hardware
    attachInterrupt(digitalPinToInterrupt(TOUCH_PIN), touchInterruptHandler, CHANGE);
    
    // Vactrol PWM Setup (LEDC)
    ledcSetup(VACTROL_PWM_CHANNEL, VACTROL_PWM_FREQ, VACTROL_PWM_RESOLUTION);
    ledcAttachPin(VACTROL_PWM_PIN, VACTROL_PWM_CHANNEL);
    ledcWrite(VACTROL_PWM_CHANNEL, 0);  // Start at 0%
    
    // Enable watchdog timer (8 second timeout)
    esp_task_wdt_init(8, true);
    esp_task_wdt_add(NULL);
    
    log_i("Hardware initialized");
}

/* ============================================================================
 * WIFI & WEB SERVER SETUP
 * ============================================================================ */

void initWiFiAndWebServer() {
    // Start in AP mode for configuration access
    WiFi.mode(WIFI_AP);
    WiFi.softAP(g_config.wifi_ssid, g_config.wifi_password);
    
    IPAddress ap_ip = WiFi.softAPIP();
    log_i("AP Mode started: SSID='%s', IP=%s", g_config.wifi_ssid, ap_ip.toString().c_str());
    
    // Start web server
    if (!g_web_server.begin()) {
        log_e("Failed to start web server!");
    }
}

/* ============================================================================
 * TELEMETRY PACKET CONSTRUCTION
 * ============================================================================ */

TelemetryPacket buildTelemetryPacket() {
    TelemetryPacket packet;
    packet.timestamp_ms = millis();
    packet.state = g_state_machine.getState();
    
    // Radar data (latest parsed frame - const reference, does not consume flags)
    const RadarFrame& frame = g_radar.getLatestFrame();
    packet.distance_raw = frame.detection_distance_cm;
    packet.distance_filtered = g_dsp.getDistanceFiltered();
    
    for (int i = 0; i < RADAR_GATE_COUNT; i++) {
        packet.stationary_energy[i] = frame.stationary_gate_energy[i];
    }
    
    // DSP data
    packet.biquad_raw = g_dsp.getBiquadRaw();
    packet.agc_normalized = g_dsp.getAGCNormalized();
    packet.peak_gate = g_dsp.getPeakGate();
    
    // State machine outputs
    packet.pwm_output = g_state_machine.getPWMOutput();
    packet.base_pwm = g_state_machine.getBasePWM();
    packet.gamma_shaped = g_state_machine.getGammaShaped();
    packet.pi_trigger = g_state_machine.getPiTrigger();
    
    return packet;
}

/* ============================================================================
 * CONFIGURATION CALLBACK (from WebServer)
 * ============================================================================ */

// This would be called from WebServer when config changes via WebSocket
// For now, we'll poll a flag in the main loop
volatile bool g_config_changed = false;
volatile bool g_config_reset_requested = false;

void onConfigChanged() {
    g_config_changed = true;
}

void onConfigReset() {
    g_config_reset_requested = true;
}

/* ============================================================================
 * SETUP
 * ============================================================================ */

void setup() {
    // Serial for debug
    Serial.begin(115200);
    delay(100);  // Allow serial to connect
    
    log_i("\n\n========================================");
    log_i("  The Apparatus - Firmware v1.0");
    log_i("  Interactive Multimedia Art Installation");
    log_i("  ESP32 + HLK-LD2410 + Vactrol Crossfader");
    log_i("========================================");
    
    // Load configuration from NVS
    loadConfiguration();
    
    // Initialize hardware
    initHardware();
    
    // Initialize radar (configures Engineering Mode 0x62)
    if (!g_radar.begin()) {
        log_e("Radar initialization failed! Continuing without radar...");
        // Blink error pattern
        for (int i = 0; i < 10; i++) {
            digitalWrite(STATUS_LED_PIN, HIGH);
            delay(100);
            digitalWrite(STATUS_LED_PIN, LOW);
            delay(100);
        }
    } else {
        log_i("Radar initialized successfully");
    }
    
    // Initialize WiFi and Web Server
    initWiFiAndWebServer();
    
    // Initialize DSP pipeline
    g_dsp.reset();
    
    // Initial status
    g_state_machine.printState();
    g_radar.printStatus();
    
    log_i("Setup complete. Entering main loop...");
    log_i("Web UI: http://%s", WiFi.softAPIP().toString().c_str());
    
    g_loop_start_time = millis();
    g_last_status_print = millis();
}

/* ============================================================================
 * MAIN LOOP
 * ============================================================================ */

void loop() {
    uint32_t loop_start = millis();
    
    // Feed watchdog
    esp_task_wdt_reset();
    
    // === 1. Update Radar (read UART, parse frames) ===
    g_radar.update();
    
    // === 2. Get Touch State (atomic read) ===
    bool touch_active = false;
    portENTER_CRITICAL(&g_touch_mux);
    touch_active = g_touch_triggered;
    portEXIT_CRITICAL(&g_touch_mux);
    
    // === 3. Process Radar Frame & DSP Pipeline ===
    bool dsp_ready = false;
    float dsp_normalized = 0.0f;
    float dsp_biquad_raw = 0.0f;
    
    RadarFrame current_frame = g_radar.getLatestFrame();  // Copy for state machine
    if (g_radar.hasNewFrame()) {
        g_radar.clearNewFrameFlag();
        dsp_ready = g_dsp.process(current_frame, dsp_normalized, dsp_biquad_raw);
    }
    
    // === 4. Update State Machine ===
    g_state_machine.update(touch_active, current_frame, dsp_ready, dsp_normalized, dsp_biquad_raw);
    
    // === 5. Update FX Relays ===
    g_fx_relays.update();
    
    // === 6. Handle Configuration Changes ===
    if (g_config_changed) {
        g_config_changed = false;
        saveConfiguration();
        g_web_server.notifyConfigChanged();
        log_i("Configuration updated and saved");
    }
    
    if (g_config_reset_requested) {
        g_config_reset_requested = false;
        resetConfiguration();
        g_web_server.notifyConfigChanged();
        g_dsp.reset();
        ESP.restart();  // Clean restart after reset
    }
    
    // === 6. Broadcast Telemetry (20 Hz) ===
    static uint32_t last_telemetry = 0;
    if (millis() - last_telemetry >= 50) {  // 20 Hz = 50ms
        TelemetryPacket packet = buildTelemetryPacket();
        g_web_server.broadcastTelemetry(packet);
        last_telemetry = millis();
    }
    
    // === 7. Update Web Server (cleanup, etc.) ===
    g_web_server.update();
    
    // === 8. Status LED Blink (heartbeat) ===
    static uint32_t last_led_toggle = 0;
    static bool led_state = false;
    if (millis() - last_led_toggle > 1000) {
        led_state = !led_state;
        digitalWrite(STATUS_LED_PIN, led_state);
        last_led_toggle = millis();
    }
    
    // === 9. Periodic Status Print (every 10 seconds) ===
    if (millis() - g_last_status_print > 10000) {
        TelemetryPacket status_packet = buildTelemetryPacket();
        log_i("=== Status @ %lu ms ===", millis());
        log_i("State: %s", g_state_machine.getStateName());
        log_i("PWM: %d, Pi Trigger: %s", g_state_machine.getPWMOutput(), 
              g_state_machine.getPiTrigger() ? "HIGH" : "LOW");
        log_i("Dist: raw=%d filtered=%.1f", status_packet.distance_raw, status_packet.distance_filtered);
        log_i("DSP: biquad=%.3f agc=%.3f", status_packet.biquad_raw, status_packet.agc_normalized);
        log_i("Radar frames: %lu, errors: %lu", g_radar.getFramesReceived(), g_radar.getParseErrors());
        log_i("WS Clients: %d", g_web_server.getWebSocketClientCount());
        g_fx_relays.printStatus();
        g_last_status_print = millis();
    }
    
    // === 10. Loop Timing Control ===
    uint32_t loop_time = millis() - loop_start;
    if (loop_time < MAIN_LOOP_TARGET_MS) {
        delay(MAIN_LOOP_TARGET_MS - loop_time);
    } else if (loop_time > MAIN_LOOP_TARGET_MS * 2) {
        log_w("Main loop overrun: %lu ms (target %d ms)", loop_time, MAIN_LOOP_TARGET_MS);
    }
}