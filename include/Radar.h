/**
 * @file Radar.h
 * @brief HLK-LD2410 mmWave Radar Driver for Engineering Mode
 *
 * Native UART parsing per HLK-LD2410 Serial Communication Protocol v1.02+.
 * Verified against ESPHome ld2410 component and shabaz123/LD2410 reference.
 *
 * Report frame (radar -> host):
 *   F4 F3 F2 F1 | len(2, LE) | type | data... | F8 F7 F6 F5
 *   type 0x02 = basic mode, type 0x01 = engineering mode
 *   Engineering data: AA | state(1) mov_dist(2) mov_e(1) still_dist(2)
 *     still_e(1) det_dist(2) maxmov_gate(1) maxstill_gate(1)
 *     moving_gate_e[9] stationary_gate_e[9] | 55 00
 *
 * ACK frame (radar -> host, in response to commands):
 *   FD FC FB FA | len(2, LE) | cmd_word(2) | 01 00 | status(2) [value...] | 04 03 02 01
 */

#ifndef RADAR_H
#define RADAR_H

#include "Config.h"

/* ============================================================================
 * RADAR DRIVER CLASS
 * ============================================================================ */

class RadarDriver {
public:
    RadarDriver();

    bool begin();
    void end();

    // Main update loop - call frequently (non-blocking)
    void update();
    // Data access
    bool hasNewFrame() const { return _new_frame_ready; }
    const RadarFrame& getLatestFrame() const { return _latest_frame; }
    // Consume the new-frame flag without copying
    void clearNewFrameFlag() { _new_frame_ready = false; }

    // Statistics / debug
    void printStatus() const;
    uint32_t getFramesReceived() const { return _frames_received; }
    uint32_t getParseErrors() const { return _parse_errors; }
    uint32_t getAcksReceived() const { return _acks_received; }

private:
    // Ring buffer for incoming UART bytes
    static constexpr size_t RX_BUFFER_SIZE = 512;
    uint8_t _rx_buffer[RX_BUFFER_SIZE];
    volatile size_t _rx_head = 0;
    volatile size_t _rx_tail = 0;

    // Frame assembly buffer (largest frame: engineering report = 4+2+36+4 = 46 bytes)
    static constexpr size_t FRAME_BUFFER_SIZE = 64;
    uint8_t _frame_buffer[FRAME_BUFFER_SIZE];
    size_t _frame_len = 0;

    // Latest parsed frame
    RadarFrame _latest_frame;
    bool _new_frame_ready = false;

    // Statistics
    uint32_t _frames_received = 0;
    uint32_t _parse_errors = 0;
    uint32_t _acks_received = 0;
    uint32_t _last_frame_time = 0;

    // UART ingestion
    void _readUartToBuffer();
    int _readByte();

    // Frame extraction state machine
    enum class ParseState : uint8_t {
        SYNC_HEADER,      // Hunting for F4 F3 F2 F1 (report) or FD FC FB FA (ACK)
        READ_LENGTH,      // Collect 2 length bytes
        COLLECT_DATA,     // Collect 'length' bytes of frame data
        CHECK_FOOTER      // Verify 4-byte footer
    };
    ParseState _state = ParseState::SYNC_HEADER;
    uint8_t  _sync_pos = 0;          // Progress through header match
    uint16_t _expected_data_len = 0;
    bool _frame_is_ack = false;

    void _processByte(uint8_t byte);
    void _handleReportFrame(const uint8_t* data, uint16_t len);
    void _handleAckFrame(const uint8_t* data, uint16_t len);
    bool _parseEngineeringPayload(const uint8_t* p, uint16_t len);

    // Command transmission + ACK matching
    void _sendCommand(uint16_t cmd_id, const uint8_t* params = nullptr, uint8_t param_len = 0);
    bool _waitAckFor(uint16_t cmd_id, uint32_t timeout_ms);

    // Pending-ACK matching state
    uint16_t _pending_cmd = 0;
    volatile bool _pending_result_valid = false;
    volatile bool _pending_result_ok = false;

    // === SIMULATION MODE (APPARATUS_SIM_MODE) ===
    // Generates a synthetic 60 s scenario at true 10 Hz so the full DSP +
    // state machine + WebUI can be exercised with no radar attached.
#ifdef APPARATUS_SIM_MODE
public:
    bool isSimulating() const { return true; }
private:
    void _simUpdate();
    static constexpr uint32_t SIM_PERIOD_MS = 100;      // 10 Hz frame rate
    static constexpr uint32_t SIM_CYCLE_MS  = 60000;    // 60 s scenario loop
    uint32_t _sim_last_frame_ms = 0;
    float    _sim_phase_rad = 0.0f;                     // breathing oscillator phase

    void _simFillGates(RadarFrame& f, float dist_cm, uint8_t base_energy);
#endif
};

#endif // RADAR_H