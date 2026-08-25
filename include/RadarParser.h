/**
 * @file RadarParser.h
 * @brief HLK-LD2410 Engineering Mode UART driver
 *
 * Report frame (radar -> host):
 *   F4 F3 F2 F1 | len(2 LE) | type | AA | payload | 55 00 | F8 F7 F6 F5
 *   type 0x01 = engineering, 0x02 = basic
 * Engineering payload: state(1) mov_dist(2) mov_E(1) still_dist(2) still_E(1)
 *   det_dist(2) maxmov_gate(1) maxstill_gate(1) mov_gates(9x1) still_gates(9x1)
 *
 * ACK frames matched to pending commands by command word.
 */

#ifndef RADAR_PARSER_H
#define RADAR_PARSER_H

#include "PinDefinitions.h"

class RadarParser {
public:
    RadarParser() = default;

    bool begin();
    void end();
    void update();

    bool hasNewFrame() const { return _new_frame_ready; }
    const RadarFrame& getLatestFrame() const { return _latest_frame; }
    void clearNewFrameFlag() { _new_frame_ready = false; }

    uint32_t getFramesReceived() const { return _frames_received; }
    uint32_t getParseErrors() const { return _parse_errors; }
    void printStatus() const;

private:
    static constexpr size_t RX_BUFFER_SIZE = 512;
    uint8_t _rx_buffer[RX_BUFFER_SIZE];
    volatile size_t _rx_head = 0;
    volatile size_t _rx_tail = 0;

    static constexpr size_t FRAME_BUFFER_SIZE = 64;
    uint8_t _frame_buffer[FRAME_BUFFER_SIZE];
    size_t _frame_len = 0;

    RadarFrame _latest_frame;
    bool _new_frame_ready = false;

    uint32_t _frames_received = 0;
    uint32_t _parse_errors = 0;
    uint32_t _acks_received = 0;
    uint32_t _last_frame_time = 0;

    void _readUartToBuffer();
    int _readByte();

    enum class ParseState : uint8_t {
        SYNC_HEADER, READ_LENGTH, COLLECT_DATA, CHECK_FOOTER
    };
    ParseState _state = ParseState::SYNC_HEADER;
    uint8_t _sync_pos = 0;
    uint16_t _expected_data_len = 0;
    bool _frame_is_ack = false;

    void _processByte(uint8_t byte);
    void _handleReportFrame(const uint8_t* data, uint16_t len);
    void _handleAckFrame(const uint8_t* data, uint16_t len);
    bool _parseEngineeringPayload(const uint8_t* p, uint16_t len);

    void _sendCommand(uint16_t cmd_id, const uint8_t* params = nullptr,
                      uint8_t param_len = 0);
    bool _waitAckFor(uint16_t cmd_id, uint32_t timeout_ms);

    uint16_t _pending_cmd = 0;
    volatile bool _pending_result_valid = false;
    volatile bool _pending_result_ok = false;

#ifdef APPARATUS_SIM_MODE
public:
    bool isSimulating() const { return true; }
private:
    void _simUpdate();
    static constexpr uint32_t SIM_PERIOD_MS = 100;
    static constexpr uint32_t SIM_CYCLE_MS  = 60000;
    void _simFillGates(RadarFrame& f, float dist_cm, uint8_t base_energy);
#endif
};

#endif // RADAR_PARSER_H