/**
 * @file Radar.cpp
 * @brief HLK-LD2410 Engineering Mode UART Driver - protocol-verified implementation
 */

#include "Radar.h"
#include <Arduino.h>

/* ============================================================================
 * CONSTRUCTOR
 * ============================================================================ */

RadarDriver::RadarDriver() {}

/* ============================================================================
 * INITIALIZATION / SHUTDOWN
 * ============================================================================ */

bool RadarDriver::begin() {
#ifdef APPARATUS_SIM_MODE
    log_i("=== SIMULATION MODE - no radar hardware required ===");
    log_i("Scenario: 0-12s approach | 12-48s stationary+breathing | 48-60s retreat");
    return true;
#else
    Serial2.begin(RADAR_BAUD_RATE, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
    if (!Serial2) {
        log_e("Failed to initialize UART2 for radar");
        return false;
    }
    Serial2.setRxBufferSize(RX_BUFFER_SIZE);

    delay(250);  // Allow radar module to boot after power-up

    // Configuration sequence per datasheet §2.4.1:
    //   enable config -> enable engineering mode (0x62) -> disable config
    if (!_waitAckFor(RADAR_CMD_ENABLE_CONFIG, 0)) {
        _sendCommand(RADAR_CMD_ENABLE_CONFIG, (const uint8_t*)"\x01\x00", 2);
        if (!_waitAckFor(RADAR_CMD_ENABLE_CONFIG, 1000)) {
            log_e("Radar did not ACK config-mode entry - continuing without radar config");
            return false;
        }
    }

    _sendCommand(RADAR_CMD_ENG_MODE_ON);  // 0x62: engineering mode ON
    bool eng_ok = _waitAckFor(RADAR_CMD_ENG_MODE_ON, 1000);

    _sendCommand(RADAR_CMD_DISABLE_CONFIG);
    _waitAckFor(RADAR_CMD_DISABLE_CONFIG, 500);

    // Engineering-mode setting is volatile (lost on radar power cycle),
    // but begin() runs at every ESP32 boot, so it is re-applied each start.
    if (!eng_ok) {
        log_e("Engineering mode NOT confirmed - gate energies will be absent");
        return false;
    }

    log_i("LD2410 initialized: Engineering Mode ON @ %d baud", RADAR_BAUD_RATE);
    return true;
#endif // APPARATUS_SIM_MODE
}

void RadarDriver::end() {
#ifndef APPARATUS_SIM_MODE
    Serial2.end();
#endif
}

/* ============================================================================
 * UART INGESTION
 * ============================================================================ */

void RadarDriver::_readUartToBuffer() {
    while (Serial2.available()) {
        int b = Serial2.read();
        if (b < 0) break;
        size_t next = (_rx_head + 1) % RX_BUFFER_SIZE;
        if (next == _rx_tail) {
            _rx_tail = (_rx_tail + 1) % RX_BUFFER_SIZE;  // Overwrite oldest on overflow
        }
        _rx_buffer[_rx_head] = (uint8_t)b;
        _rx_head = next;
    }
}

int RadarDriver::_readByte() {
    if (_rx_tail == _rx_head) return -1;
    uint8_t b = _rx_buffer[_rx_tail];
    _rx_tail = (_rx_tail + 1) % RX_BUFFER_SIZE;
    return b;
}

/* ============================================================================
 * MAIN UPDATE - byte-stream frame extractor (or SIM generator)
 * ============================================================================ */

void RadarDriver::update() {
#ifdef APPARATUS_SIM_MODE
    _simUpdate();
#else
    _readUartToBuffer();
    int b;
    while ((b = _readByte()) >= 0) {
        _processByte((uint8_t)b);
    }
#endif
}

/* ============================================================================
 * SIMULATION MODE - synthetic 60 s scenario at true 10 Hz
 *
 *   t=0-12s   : approach 420 -> 140 cm  (MACRO: distance->PWM mapping)
 *   t=12-48s  : stationary @ ~150 cm    (MICRO: breathing modulation)
 *               - gate energies oscillate at 0.25 Hz (15 breaths/min)
 *               - small position sway (+/-2 cm)
 *   t=48-60s  : retreat 140 -> 420 cm   (MACRO -> IDLE)
 *   One outlier spike (+180 cm @ t=30s) per cycle to demonstrate EMA rejection.
 * ============================================================================ */

#ifdef APPARATUS_SIM_MODE

void RadarDriver::_simUpdate() {
    uint32_t now = millis();
    if (_latest_frame.timestamp_ms != 0 && now - _latest_frame.timestamp_ms < SIM_PERIOD_MS) return;

    RadarFrame f;
    f.timestamp_ms = now;
    f.valid = true;

    uint32_t t = now % SIM_CYCLE_MS;
    float t_s = t / 1000.0f;

    // --- Distance profile ---
    float dist;
    if (t < 12000) {                       // Approach: 420 -> 140 over 12 s
        float k = t / 12000.0f;
        dist = 420.0f - 280.0f * k;
        f.target_state = TARGET_STATE_MOVING;
    } else if (t < 48000) {                // Stationary with slight sway (+/-2 cm)
        dist = 150.0f + 2.0f*sinf(2.0f*PI*0.13f*t_s);
        f.target_state = TARGET_STATE_STATIONARY;
    } else {                               // Retreat: 140 -> 420 over 12 s
        float k = (t - 48000) / 12000.0f;
        dist = 140.0f + 280.0f * k;
        f.target_state = TARGET_STATE_MOVING;
    }

    // --- Injected outlier spike (once per cycle, at t=30s, +180 cm) ---
    static uint32_t last_cycle_seen = UINT32_MAX;
    static bool spike_done = false;
    uint32_t cycle = now / SIM_CYCLE_MS;
    if (cycle != last_cycle_seen) { last_cycle_seen = cycle; spike_done = false; }
    bool spike_now = false;
    if (!spike_done && t >= 30000 && t < 30100) {
        dist += 180.0f;                    // Sudden 1.8 m phantom jump
        spike_now = true;
        spike_done = true;
    }

    f.detection_distance_cm = (uint16_t)(dist + 0.5f);
    f.moving_distance_cm = f.detection_distance_cm;
    if (f.target_state == TARGET_STATE_STATIONARY) {
        f.stationary_distance_cm = f.detection_distance_cm;
    }

    // --- Breathing-modulated gate energies (0.25 Hz = 15 breaths/min) ---
    float breath_val = sinf(2.0f*PI*0.25f*t_s);       // -1..+1
    uint8_t base_e = (uint8_t)(50 + 25*breath_val);   // 25..75 oscillation

    _simFillGates(f, dist, base_e);

    f.moving_energy = (f.target_state == TARGET_STATE_MOVING) ? 60 : 5;
    f.stationary_energy = (f.target_state == TARGET_STATE_STATIONARY)
                          ? (uint8_t)(40 + 20*breath_val) : 0;

    if (spike_now) log_w("SIM: injected outlier spike +180 cm");

    _latest_frame = f;
    _new_frame_ready = true;
}

// Concentrate energy in gates around the target position with triangular
// falloff so peak-gate selection has a meaningful maximum.
void RadarDriver::_simFillGates(RadarFrame& f, float dist_cm, uint8_t base_energy) {
    for (int i = 0; i < RADAR_GATE_COUNT; i++) {
        float gate_center = i*RADAR_GATE_SIZE_CM + RADAR_GATE_SIZE_CM/2.0f;
        float d = fabsf(dist_cm - gate_center) / RADAR_GATE_SIZE_CM;
        float e = base_energy * constrain(1.0f - d, 0.0f, 1.0f);
        f.stationary_gate_energy[i] = (uint8_t)(e + 0.5f);
        f.moving_gate_energy[i]     = (uint8_t)(e*0.4f + 0.5f);
    }
}

#endif // APPARATUS_SIM_MODE

void RadarDriver::_processByte(uint8_t byte) {
    switch (_state) {

        case ParseState::SYNC_HEADER: {
            static const uint8_t report_hdr[4] = {0xF4, 0xF3, 0xF2, 0xF1};
            static const uint8_t ack_hdr[4]    = {0xFD, 0xFC, 0xFB, 0xFA};

            if (byte == report_hdr[_sync_pos]) {
                _frame_is_ack = false;
            } else if (byte == ack_hdr[_sync_pos]) {
                _frame_is_ack = true;
            } else {
                _sync_pos = 0;
                return;  // Not a header byte
            }

            _frame_buffer[_sync_pos++] = byte;

            if (_sync_pos == 4) {
                _sync_pos = 0;
                _state = ParseState::READ_LENGTH;
                _frame_len = 4;
            }
            break;
        }

        case ParseState::READ_LENGTH:
            _frame_buffer[_frame_len++] = byte;
            if (_frame_len == 6) {
                _expected_data_len = _frame_buffer[4] | (_frame_buffer[5] << 8);
                if (_expected_data_len == 0 || _expected_data_len > FRAME_BUFFER_SIZE - 10) {
                    // Implausible length - resync
                    _parse_errors++;
                    _state = ParseState::SYNC_HEADER;
                    _frame_len = 0;
                } else {
                    _state = ParseState::COLLECT_DATA;
                }
            }
            break;

        case ParseState::COLLECT_DATA:
            _frame_buffer[_frame_len++] = byte;
            if (--_expected_data_len == 0) {
                _state = ParseState::CHECK_FOOTER;
            }
            break;

        case ParseState::CHECK_FOOTER:
            _frame_buffer[_frame_len++] = byte;
            if (_frame_len == 14 + 0 && false) { /* unreachable guard */ }
            if (_frame_len >= 10 &&
                _frame_buffer[_frame_len - 4] == 0xF8 &&
                _frame_buffer[_frame_len - 3] == 0xF7 &&
                _frame_buffer[_frame_len - 2] == 0xF6 &&
                _frame_buffer[_frame_len - 1] == 0xF5 &&
                _frame_len == (size_t)(6 + (_frame_buffer[4] | (_frame_buffer[5] << 8)) + 4)) {
                // Complete frame with valid footer
                const uint8_t* data = &_frame_buffer[6];
                uint16_t dlen = _frame_len - 10;
                if (_frame_is_ack) {
                    _handleAckFrame(data, dlen);
                } else {
                    _handleReportFrame(data, dlen);
                }
                _state = ParseState::SYNC_HEADER;
                _frame_len = 0;
            } else if (_frame_len > FRAME_BUFFER_SIZE - 2) {
                // Ran off the end without a valid footer - resync
                _parse_errors++;
                _state = ParseState::SYNC_HEADER;
                _frame_len = 0;
            } else {
                _state = ParseState::SYNC_HEADER;  // Footer mismatch - resync
                _frame_len = 0;
                _parse_errors++;
            }
            break;
    }
}

/* ============================================================================
 * REPORT FRAME HANDLING
 * ============================================================================ */

void RadarDriver::_handleReportFrame(const uint8_t* data, uint16_t len) {
    if (len < 3 || data[1] != REPORT_HEAD_MARKER) {
        _parse_errors++;
        return;
    }

    uint8_t type = data[0];

    if (type == RADAR_REPORT_ENGINEERING) {
        if (_parseEngineeringPayload(data + 1, len - 1)) {
            _frames_received++;
            _last_frame_time = millis();
            _new_frame_ready = true;
        }
    } else if (type == RADAR_REPORT_BASIC) {
        // Basic mode still gives us state + distances (no gates).
        // Parse the basic block so the pipeline degrades gracefully.
        // AA | state(1) mov(2) movE(1) still(2) stillE(1) det(2) | 55 00 = 13 bytes payload
        if (len >= 13) {
            _latest_frame.target_state          = data[2];
            _latest_frame.moving_distance_cm    = data[3] | (data[4] << 8);
            _latest_frame.moving_energy         = data[5];
            _latest_frame.stationary_distance_cm= data[6] | (data[7] << 8);
            _latest_frame.stationary_energy     = data[8];
            _latest_frame.detection_distance_cm = data[9] | (data[10] << 8);
            _latest_frame.timestamp_ms = millis();
            _latest_frame.valid = true;
            _frames_received++;
            _last_frame_time = millis();
            _new_frame_ready = true;
        }
    }
}

bool RadarDriver::_parseEngineeringPayload(const uint8_t* p, uint16_t len) {
    // p points at 0xAA. Expected layout (35 bytes incl. AA ... 55 00):
    // [0]=AA [1]=state [2..3]=mov_dist [4]=mov_e [5..6]=still_dist [7]=still_e
    // [8..9]=det_dist [10]=max_mov_gate [11]=max_still_gate
    // [12..20]=moving_gate_e x9 [21..29]=still_gate_e x9 [30]=55 [31]=00

    if (len < ENG_PAYLOAD_LEN || p[0] != REPORT_HEAD_MARKER) {
        _parse_errors++;
        return false;
    }

    _latest_frame.target_state           = p[1];
    _latest_frame.moving_distance_cm     = p[2] | (p[3] << 8);
    _latest_frame.moving_energy          = p[4];
    _latest_frame.stationary_distance_cm = p[5] | (p[6] << 8);
    _latest_frame.stationary_energy      = p[7];
    _latest_frame.detection_distance_cm  = p[8] | (p[9] << 8);
    _latest_frame.max_moving_gate        = p[10];
    _latest_frame.max_stationary_gate    = p[11];

    for (int i = 0; i < RADAR_GATE_COUNT; i++) {
        _latest_frame.moving_gate_energy[i]     = p[12 + i];      // Single bytes!
        _latest_frame.stationary_gate_energy[i] = p[21 + i];
    }

    _latest_frame.timestamp_ms = millis();
    _latest_frame.valid = true;
    return true;
}

/* ============================================================================
 * COMMAND TRANSMISSION & ACK MATCHING
 * ============================================================================ */

void RadarDriver::_sendCommand(uint16_t cmd_id, const uint8_t* params, uint8_t param_len) {
    uint8_t frame[32];
    size_t pos = 0;

    memcpy(&frame[pos], CMD_HEADER, 4); pos += 4;

    uint16_t data_len = 2 + param_len;
    frame[pos++] = data_len & 0xFF;
    frame[pos++] = (data_len >> 8) & 0xFF;

    frame[pos++] = cmd_id & 0xFF;
    frame[pos++] = (cmd_id >> 8) & 0xFF;

    if (params && param_len > 0) {
        memcpy(&frame[pos], params, param_len);
        pos += param_len;
    }

    memcpy(&frame[pos], CMD_FOOTER, 4); pos += 4;

    Serial2.write(frame, pos);
    Serial2.flush();
}

bool RadarDriver::_waitAckFor(uint16_t cmd_id, uint32_t timeout_ms) {
    if (timeout_ms == 0) {
        // Fire-and-forget mode: just flush any stale bytes
        while (_readByte() >= 0) {}
        return false;
    }

    _pending_cmd = cmd_id;
    _pending_result_valid = false;
    _pending_result_ok = false;

    uint32_t start = millis();
    while ((millis() - start) < timeout_ms) {
        update();  // Runs parser, which resolves pending ACK via _handleAckFrame()
        if (_pending_result_valid) {
            if (!_pending_result_ok) {
                log_w("Radar NACK for command 0x%04X", cmd_id);
            }
            return _pending_result_ok;
        }
        delay(2);
    }
    log_w("ACK timeout (0x%04X) after %lu ms", cmd_id, (unsigned long)timeout_ms);
    return false;
}

void RadarDriver::_handleAckFrame(const uint8_t* data, uint16_t len) {
    // data: cmd_word(2 LE) | 01 00 | status(2 LE) | optional value bytes
    if (len < 6) {
        _parse_errors++;
        return;
    }

    uint16_t cmd_word = data[0] | (data[1] << 8);
    uint16_t ack_marker = data[2] | (data[3] << 8);
    uint16_t status = data[4] | (data[5] << 8);

    if (ack_marker != RADAR_ACK_WORD) {
        _parse_errors++;
        return;
    }

    _acks_received++;

    if (_pending_cmd != 0 && cmd_word == _pending_cmd) {
        _pending_result_valid = true;
        _pending_result_ok = (status == RADAR_ACK_STATUS_SUCCESS);
        _pending_cmd = 0;
    }
}

/* ============================================================================
 * DEBUG
 * ============================================================================ */

void RadarDriver::printStatus() const {
    log_i("Radar: frames=%lu acks=%lu errors=%lu last=%lums ago",
          (unsigned long)_frames_received, (unsigned long)_acks_received,
          (unsigned long)_parse_errors,
          (unsigned long)(millis() - _last_frame_time));
}